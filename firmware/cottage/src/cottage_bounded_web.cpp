#ifndef COTTAGE_BENCH_MODE
#include "cottage_bounded_web.hpp"
#include <Arduino.h>
#include <cerrno>
#include <cstring>
#include <lwip/sockets.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>

using namespace enevina::cottage;

namespace {
bool copy_text(String& out,std::string_view view) {
    out="";
    return view.empty() || (out.reserve(view.size()) && out.concat(view.data(),view.size()));
}
bool decode_text(String& out,std::string_view view) {
    out="";
    return out.reserve(view.size()) && decode_http_form(view,[&](char c){return out.concat(c);});
}
}
void CottageWebServer::finishRequest() {
    _currentClient.stop();
    _currentStatus=HC_NONE;
    _currentHandler=nullptr;
    _currentUri="";
    _hostHeader="";
    _responseHeaders="";
    _clientContentLength=0;
    _chunked=false;
    delete[] _currentArgs;_currentArgs=nullptr;_currentArgCount=0;
    for(int i=0;i<_headerKeysCount;++i)_currentHeaders[i].value="";
    if(request_buffer_) {
        // Forms may contain credentials. Erase only the used bytes before
        // freeing the request-local PSRAM allocation.
        volatile char* bytes=request_buffer_;
        for(size_t i=0;i<ingress_.used();++i)bytes[i]=0;
        heap_caps_free(request_buffer_);request_buffer_=nullptr;
    }
    ingress_.reset();
}
void CottageWebServer::close() {
    finishRequest();
    WebServer::close();
}
void CottageWebServer::rejectRequest(const char* response) {
    ++request_rejects_;
    const int fd=_currentClient.fd();
    // A malformed or slow client must never get another waiting loop.
    // The short rejection is best effort; closing is authoritative.
    if(fd>=0 && response)::send(fd,response,std::strlen(response),MSG_DONTWAIT);
    finishRequest();
}
bool CottageWebServer::prepareRequest() {
    const auto& head=ingress_.head();
    const auto body=ingress_.body(request_buffer_);
    size_t count=0;
    const auto count_field=[&](std::string_view,std::string_view){return ++count<=web_argument_limit;};
    if(!each_http_field(head.query,count_field))return false;
    if(head.body==HttpBody::Form && !each_http_field(body,count_field))return false;
    if(head.body==HttpBody::Plain && ++count>web_argument_limit)return false;
    auto args=std::unique_ptr<RequestArgument[]>(new(std::nothrow) RequestArgument[count+1]);
    if(!args)return false;
    size_t used=0;
    const auto add=[&](std::string_view key,std::string_view value) {
        auto& arg=args[used];
        if(!decode_text(arg.key,key)||!decode_text(arg.value,value)||!arg.key.length())return false;
        // Reject duplicate decoded keys across query and form instead of
        // silently choosing one of two conflicting settings/token values.
        for(size_t i=0;i<used;++i)if(args[i].key==arg.key)return false;
        ++used;return true;
    };
    if(!each_http_field(head.query,add))return false;
    if(head.body==HttpBody::Form && !each_http_field(body,add))return false;
    if(head.body==HttpBody::Plain) {
        for(size_t i=0;i<used;++i)if(args[i].key=="plain")return false;
        if(body.find('\0')!=std::string_view::npos ||
           !copy_text(args[used].key,"plain")||!copy_text(args[used].value,body))return false;
        ++used;
    }
    if(!copy_text(_currentUri,head.path)||!copy_text(_hostHeader,head.host))return false;
    // Retain the framework's collected-header semantics without invoking
    // its blocking request parser or unchecked argument allocator.
    auto headers=head.headers;
    while(!headers.empty()) {
        const auto end=headers.find("\r\n");
        std::string_view key,value;
        if(!split_header(headers.substr(0,end),key,value))return false;
        headers.remove_prefix(end+2);
        for(int i=0;i<_headerKeysCount;++i) {
            if(iequal(key,{_currentHeaders[i].key.c_str(),_currentHeaders[i].key.length()}) &&
               !copy_text(_currentHeaders[i].value,value))return false;
        }
    }
    delete[] _currentArgs;
    _currentArgs=args.release();_currentArgCount=used;
    _currentMethod=head.verb==HttpVerb::Get?HTTP_GET:HTTP_POST;
    _currentVersion=head.minor;
    _clientContentLength=head.length;
    _contentLength=CONTENT_LENGTH_NOT_SET;
    _currentHandler=nullptr;
    for(auto* handler=_firstHandler;handler;handler=handler->next()) {
        if(handler->canHandle(_currentMethod,_currentUri)){_currentHandler=handler;break;}
    }
    return true;
}
void CottageWebServer::handleClient() {
    if(_currentStatus==HC_NONE) {
        _currentClient=_server.available();
        if(_currentClient.fd()<0){if(_nullDelay)delay(1);return;}
        request_started_=millis();
        _currentStatus=HC_WAIT_READ;
        request_buffer_=static_cast<char*>(heap_caps_malloc(web_request_capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!request_buffer_) {
            rejectRequest("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return;
        }
    }
    if(request_expired(millis(),request_started_)) {
        ++request_timeouts_;rejectRequest(nullptr);return;
    }
    const int fd=_currentClient.fd();
    if(fd<0){finishRequest();return;}
    const auto count=std::min(ingress_.room(),web_receive_block);
    if(!count){rejectRequest(nullptr);return;}
    // One nonblocking receive per loop; never wait for a missing byte.
    const int n=::recv(fd,request_buffer_+ingress_.used(),count,MSG_DONTWAIT);
    if(n<0) {
        if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR){delay(1);return;}
        rejectRequest(nullptr);return;
    }
    if(!n){rejectRequest(nullptr);return;}
    const auto state=ingress_.received(request_buffer_,size_t(n));
    if(state==HttpInput::Pending){delay(1);return;}
    if(state!=HttpInput::Complete||!prepareRequest()) {
        rejectRequest(state==HttpInput::TooLarge?
            "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n":
            "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        return;
    }
    if(request_expired(millis(),request_started_)) {
        ++request_timeouts_;rejectRequest(nullptr);return;
    }
    // Reception is spread over loop iterations. Give the existing response
    // path its own absolute budget when a complete validated request arrives.
    start_ms_=millis();aborted_=false;
    _handleRequest();
    finishRequest();
}
void CottageWebServer::abortResponse() {
    if(!aborted_)++aborts_;
    aborted_=true;
    // Prevent the SDK finalizer from attempting an unbounded chunk footer.
    _chunked=false;
    _currentClient.stop();
}
size_t CottageWebServer::writeBounded(const char* data,size_t size) {
    if(!size || aborted_)return 0;
    const int socket_fd=_currentClient.fd();
    if(socket_fd<0){abortResponse();return 0;}
    const uint32_t begun=millis();
    const auto sent=bounded_http_write(data,size,start_ms_,web_response_budget_ms,
        [](){return millis();},
        [socket_fd](const char* bytes,size_t count)->int {
            const int n=::send(socket_fd,bytes,count,MSG_DONTWAIT);
            if(n>=0)return n;
            return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR?0:-1;
        },
        [](){delay(5);});
    longest_write_ms_=std::max(longest_write_ms_,uint32_t(millis()-begun));
    if(sent!=size)abortResponse();
    return sent;
}
size_t CottageWebServer::_currentClientWrite(const char* data,size_t size) {
    return writeBounded(data,size);
}
size_t CottageWebServer::_currentClientWrite_P(PGM_P data,size_t size) {
    // ESP32 PROGMEM is memory mapped, exactly as WiFiClient::write_P.
    return writeBounded(data,size);
}
bool CottageWebServer::streamContent(const char* data) {
    if(aborted_ || !data)return false;
    const size_t size=std::strlen(data);
    if(!_chunked)return writeBounded(data,size)==size;
    const bool ok=bounded_http_chunk(data,size,[this](const char* bytes,size_t count){
        return writeBounded(bytes,count);
    });
    if(!ok)abortResponse();
    else if(!size)_chunked=false;
    return ok;
}
#endif
