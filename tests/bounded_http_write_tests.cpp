#include "cottage/bounded_http_write.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <chrono>
#include <thread>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace enevina::cottage;
int main(){
    std::string payload(450000,'x'); // full-sized dashboard, not just a short JSON status
    uint32_t clock=100;
    unsigned calls=0;
    auto now=[&](){return clock;};
    auto pause=[&](){clock+=5;};
    const auto stalled=[&](const char*,size_t){++calls;return 0;};
    assert(bounded_http_write(payload.data(),payload.size(),100,3500,now,stalled,pause)==0);
    assert(clock==3600 && calls==700);
    // A trickle of successful writes must NOT restart the absolute budget.
    clock=100;calls=0;
    auto trickle=[&](const char*,size_t){++calls;clock+=100;return 1;};
    assert(bounded_http_write(payload.data(),payload.size(),100,3500,now,trickle,pause)==35);
    assert(clock==3600);
    assert(bounded_http_write(payload.data(),10,100,3500,now,trickle,pause)==0);
    assert(calls==35);
    clock=UINT32_MAX-9u;
    auto start=clock;
    assert(bounded_http_write(payload.data(),10,start,20,now,stalled,pause)==0);
    assert(uint32_t(clock-start)==20);
    // Send size is bounded, all partial writes preserve byte order.
    clock=0; std::string sent;
    auto partial=[&](const char* p,size_t n){
        assert(n<=web_write_block);
        const auto count=std::min(n,size_t(1000));sent.append(p,count);
        ++clock;return int(count);
    };
    assert(bounded_http_write(payload.data(),payload.size(),0,3500,now,partial,pause)==payload.size());
    assert(sent==payload);
    clock=0;
    assert(bounded_http_write(payload.data(),20,0,3500,now,
            [](const char*,size_t){return -1;},pause)==0);
    assert(bounded_http_write(payload.data(),20,0,3500,now,
            [](const char*,size_t n){return int(n+1);},pause)==0);
    assert(bounded_http_write(nullptr,20,0,3500,now,stalled,pause)==0);
    // Every chunk component, including final CRLF and terminator, goes
    // through the same transport. A failed body cannot emit a success tail.
    std::string body;
    auto write=[&](const char* p,size_t n){body.append(p,n);return n;};
    assert(bounded_http_chunk("hello",5,write));
    assert(bounded_http_chunk("",0,write));
    assert(body=="5\r\nhello\r\n0\r\n\r\n");
    body.clear();calls=0;
    assert(!bounded_http_chunk("hello",5,[&](const char* p,size_t n)->size_t {
        ++calls;if(calls==2)return 0;body.append(p,n);return n;
    }));
    assert(calls==2 && body=="5\r\n");
    // Exercise real kernel socket backpressure with no receiver. A slow
    // browser cannot hold the writer forever even after partial progress.
#if defined(__unix__) || defined(__APPLE__)
    int fd[2]{};
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    int buffer=1024;
    assert(setsockopt(fd[0],SOL_SOCKET,SO_SNDBUF,&buffer,sizeof(buffer))==0);
    auto epoch=std::chrono::steady_clock::now();
    auto live_now=[&](){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-epoch).count());};
    auto live_send=[&](const char* p,size_t n)->int{
        const int rc=::send(fd[0],p,n,MSG_DONTWAIT);
        return rc>=0?rc:errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR?0:-1;
    };
    const auto n=bounded_http_write(payload.data(),payload.size(),0,80,live_now,live_send,
        [](){std::this_thread::sleep_for(std::chrono::milliseconds(1));});
    const auto duration=live_now();
    assert(n>0 && n<payload.size() && duration>=80 && duration<1000);
    close(fd[0]);close(fd[1]);
    std::cout<<"Real TCP-like socket backpressure: "<<n<<" bytes, stopped in "<<duration<<" ms\n";
#endif
    std::cout<<"PASS bounded HTTP response, absolute deadline, partial progress and chunk framing\n";
}
