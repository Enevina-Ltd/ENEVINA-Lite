#include "cottage_bounded_web.hpp"
#include "Arduino.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <string>
#include <iostream>
using namespace enevina::cottage;
static std::string drain(int fd) {
    std::string result;
    char buf[4096];
    for(;;){
        const auto n=::read(fd,buf,sizeof(buf));
        assert(n>=0);if(!n)break;result.append(buf,size_t(n));
    }
    close(fd);return result;
}
int main(){
    CottageWebServer web(80);
    int fd[2]{};
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    fake_clock=100;
    web.attach(fd[0],[&](){
        assert(web.header("HTTP/1.1 200 OK\r\n\r\n")==19);
        web.startChunks();
        assert(web.streamContent("hello"));
        assert(web.streamContent("world"));
        assert(web.streamContent(""));
    });
    web.handleClient();
    assert(!web.responseAborted() && web.responseAborts()==0 && web.unbounded_finalizers==0);
    assert(drain(fd[1])=="HTTP/1.1 200 OK\r\n\r\n5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n");
    // Nonreading client during chunked response. Fake monotonic time
    // advances only on pauses; the socket really applies backpressure.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    int buf=1024;assert(setsockopt(fd[0],SOL_SOCKET,SO_SNDBUF,&buf,sizeof(buf))==0);
    std::string large(450000,'z');
    web.attach(fd[0],[&](){
        web.startChunks();
        assert(!web.streamContent(large.c_str()));
        assert(web.responseAborted());
        assert(!web.streamContent(""));
    });
    auto started=fake_clock;web.handleClient();
    assert(fake_clock-started==web_response_budget_ms);
    assert(web.responseAborts()==1 && web.unbounded_finalizers==0);
    const auto truncated=drain(fd[1]);
    assert(!truncated.empty() && truncated.size()<large.size());
    assert(truncated.find("0\r\n\r\n")==std::string::npos);
    // New request must recover from the previous abort; PROGMEM route goes
    // through the other SDK virtual write hook.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){
        assert(!web.responseAborted());
        assert(web.flash("dashboard",9)==9);
    });
    web.handleClient();
    assert(!web.responseAborted() && web.responseAborts()==1);
    assert(drain(fd[1])=="dashboard");
    // Handler consumed the budget before sending a header: close rather
    // than start a new per-write timer.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){
        fake_clock+=web_response_budget_ms;
        assert(web.header("late")==0);
    });
    web.handleClient();
    assert(web.responseAborted() && web.responseAborts()==2);
    assert(drain(fd[1]).empty());
    std::cout<<"PASS actual CottageWebServer transport: chunk framing, stalled client, recovery, flash and shared budget\n";
}
