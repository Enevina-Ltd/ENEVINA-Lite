#ifndef COTTAGE_BENCH_MODE
#include "cottage_bounded_web.hpp"
#include <Arduino.h>
#include <cerrno>
#include <cstring>
#include <lwip/sockets.h>

using namespace enevina::cottage;

void CottageWebServer::handleClient() {
    start_ms_=millis();
    aborted_=false;
    WebServer::handleClient();
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
