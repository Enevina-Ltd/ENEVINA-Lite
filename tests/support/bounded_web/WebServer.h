#pragma once
// Minimal native boundary double for the pinned SDK's virtual transport
// hooks. The tested derived class is the real ESP32 source, not a replica.
#include <cstddef>
#include <functional>
#include <cstring>
#include <unistd.h>
using PGM_P=const char*;
class WiFiClient {
    int socket_=-1;
public:
    int fd()const{return socket_;}
    void attach(int fd){stop();socket_=fd;}
    void stop(){if(socket_>=0)::close(socket_);socket_=-1;}
};
class WebServer {
    std::function<void()> handler_;
public:
    explicit WebServer(int){}
    virtual ~WebServer(){_currentClient.stop();}
    virtual void handleClient(){
        if(handler_)handler_();
        // The real SDK calls an unbounded footer if _chunked remains true.
        // Track any accidental fallback as a test failure.
        if(_chunked)++unbounded_finalizers;
        _currentClient.stop();
    }
    void attach(int fd,std::function<void()> cb){
        _currentClient.attach(fd);handler_=std::move(cb);_chunked=false;
    }
    void startChunks(){_chunked=true;}
    size_t header(const char* bytes){return _currentClientWrite(bytes,std::strlen(bytes));}
    size_t flash(const char* bytes,size_t size){return _currentClientWrite_P(bytes,size);}
    unsigned unbounded_finalizers=0;
protected:
    virtual size_t _currentClientWrite(const char*,size_t)=0;
    virtual size_t _currentClientWrite_P(PGM_P,size_t)=0;
    WiFiClient _currentClient;
    bool _chunked=false;
};
