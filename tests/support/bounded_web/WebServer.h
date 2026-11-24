#pragma once
// Native boundary double for the pinned SDK: route dispatch, request state,
// and virtual response hooks. Real sockets exercise the actual derived class.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <cstring>
#include <string>
#include <utility>
#include <unistd.h>
using PGM_P=const char*;
class String {
    std::string value_;
public:
    String()=default;
    String(const char* s):value_(s?s:""){}
    String& operator=(const char* s){value_=s?s:"";return *this;}
    bool reserve(size_t n){value_.reserve(n);return true;}
    bool concat(const char* s,size_t n){value_.append(s,n);return true;}
    bool concat(char c){value_.push_back(c);return true;}
    size_t length()const{return value_.size();}
    const char* c_str()const{return value_.c_str();}
    bool operator==(const String& other)const{return value_==other.value_;}
};
class WiFiClient {
    int socket_=-1;
public:
    WiFiClient()=default;
    explicit WiFiClient(int fd):socket_(fd){}
    WiFiClient(WiFiClient&& other)noexcept:socket_(std::exchange(other.socket_,-1)){}
    WiFiClient& operator=(WiFiClient&& other)noexcept {
        if(this!=&other){stop();socket_=std::exchange(other.socket_,-1);}return *this;
    }
    ~WiFiClient(){stop();}
    int fd()const{return socket_;}
    void stop(){if(socket_>=0)::close(socket_);socket_=-1;}
};
class WiFiServer {
    int pending_=-1;
public:
    ~WiFiServer(){if(pending_>=0)::close(pending_);}
    void attach(int fd){if(pending_>=0)::close(pending_);pending_=fd;}
    WiFiClient available(){return WiFiClient(std::exchange(pending_,-1));}
};
enum HTTPMethod {HTTP_ANY,HTTP_GET,HTTP_POST};
enum HTTPClientStatus {HC_NONE,HC_WAIT_READ,HC_WAIT_CLOSE};
constexpr size_t CONTENT_LENGTH_NOT_SET=size_t(-2);
struct RequestHandler {
    std::function<void()> fn;
    RequestHandler* next(){return nullptr;}
    bool canHandle(HTTPMethod,const String&){return true;}
};
class WebServer {
    RequestHandler route_{};
public:
    explicit WebServer(int){
        _firstHandler=&route_;
        _currentHeaders=new RequestArgument[1];_headerKeysCount=1;
        _currentHeaders[0].key="Authorization";
    }
    virtual ~WebServer(){delete[] _currentArgs;delete[] _currentHeaders;}
    virtual void handleClient(){++unbounded_parsers;}
    virtual void close(){_currentStatus=HC_NONE;}
    void attach(int fd,std::function<void()> cb){_server.attach(fd);route_.fn=std::move(cb);}
    void startChunks(){_chunked=true;}
    size_t header(const char* bytes){return _currentClientWrite(bytes,std::strlen(bytes));}
    size_t flash(const char* bytes,size_t size){return _currentClientWrite_P(bytes,size);}
    String uri()const{return _currentUri;}
    String hostHeader()const{return _hostHeader;}
    HTTPMethod method()const{return _currentMethod;}
    int args()const{return _currentArgCount;}
    String arg(const char* name)const{
        for(int i=0;i<_currentArgCount;++i)if(_currentArgs[i].key==String(name))return _currentArgs[i].value;
        return "";
    }
    String collectedHeader()const{return _currentHeaders[0].value;}
    unsigned unbounded_finalizers=0,unbounded_parsers=0,dispatches=0;
protected:
    virtual size_t _currentClientWrite(const char*,size_t)=0;
    virtual size_t _currentClientWrite_P(PGM_P,size_t)=0;
    void _handleRequest(){
        ++dispatches;if(_currentHandler&&_currentHandler->fn)_currentHandler->fn();
        if(_chunked)++unbounded_finalizers;
    }
    struct RequestArgument {String key,value;};
    WiFiServer _server;
    WiFiClient _currentClient;
    HTTPClientStatus _currentStatus=HC_NONE;
    HTTPMethod _currentMethod=HTTP_ANY;
    RequestHandler* _currentHandler=nullptr;
    RequestHandler* _firstHandler=nullptr;
    String _currentUri,_hostHeader,_responseHeaders;
    uint8_t _currentVersion=0;
    size_t _contentLength=0;
    int _clientContentLength=0,_currentArgCount=0,_headerKeysCount=0;
    RequestArgument* _currentArgs=nullptr;
    RequestArgument* _currentHeaders=nullptr;
    bool _chunked=false,_nullDelay=true;
};
