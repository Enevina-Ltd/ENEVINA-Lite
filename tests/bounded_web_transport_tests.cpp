#include "cottage_bounded_web.hpp"
#include "Arduino.h"
#include "esp_heap_caps.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <string>
#include <iostream>
using namespace enevina::cottage;
static std::string drain(int fd,bool allow_reset=false) {
    std::string result;
    char buf[4096];
    for(;;){
        const auto n=::read(fd,buf,sizeof(buf));
        // Closing after a rejected peeked request may reset an unread
        // socket. Only the explicit allocation-failure test permits it.
        if(n<0 && allow_reset && errno==ECONNRESET)break;
        assert(n>=0);if(!n)break;result.append(buf,size_t(n));
    }
    close(fd);return result;
}
static void send_all(int fd,const std::string& bytes) {
    assert(::send(fd,bytes.data(),bytes.size(),0)==static_cast<ssize_t>(bytes.size()));
}
static void request(int fd) {send_all(fd,"GET / HTTP/1.1\r\nHost: lite\r\n\r\n");}
int main(){
    CottageWebServer web(80);
    int fd[2]{};
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    fake_clock=100;
    request(fd[1]);
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
    request(fd[1]);
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
    request(fd[1]);
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
    request(fd[1]);
    web.attach(fd[0],[&](){
        fake_clock+=web_response_budget_ms;
        assert(web.header("late")==0);
    });
    web.handleClient();
    assert(web.responseAborted() && web.responseAborts()==2);
    assert(drain(fd[1]).empty());
    // Slow headers and form body progress over separate loop invocations.
    const auto before=web.dispatches;
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){
        assert(web.method()==HTTP_POST && web.uri()==String("/api/settings"));
        assert(web.hostHeader()==String("lite") && web.args()==3);
        assert(web.arg("draft")==String("true") && web.arg("token")==String("a+b"));
        assert(web.arg("name")==String("Mäkinen & Co"));
        assert(web.collectedHeader()==String("Example"));
        assert(web.header("OK")==2);
    });
    const std::string form="token=a%2Bb&name=M%C3%A4kinen+%26+Co";
    const std::string head="POST /api/settings?draft=true HTTP/1.1\r\nHost: lite\r\n"
        "Authorization: Example\r\nContent-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
        "Content-Length: "+std::to_string(form.size())+"\r\n\r\n";
    send_all(fd[1],head.substr(0,15));web.handleClient();assert(web.dispatches==before);
    send_all(fd[1],head.substr(15)+form.substr(0,5));web.handleClient();
    assert(web.dispatches==before && ingress_allocations==1);
    send_all(fd[1],form.substr(5));web.handleClient();
    assert(web.dispatches==before+1 && drain(fd[1])=="OK" && ingress_allocations==0);
    // A trickling client never renews the three-second absolute deadline.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    send_all(fd[1],"G");web.handleClient();
    for(int i=0;i<2;++i){fake_clock+=900;send_all(fd[1],"E");web.handleClient();}
    fake_clock+=1200;web.handleClient();
    assert(web.requestTimeouts()==1 && web.requestRejects()==1 && ingress_allocations==0);
    close(fd[1]);
    // Invalid duplicate parameters are rejected before any physical handler.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    const std::string duplicate="token=one&%74oken=two";
    send_all(fd[1],"POST /api/control/arm HTTP/1.1\r\nHost: lite\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: "+
        std::to_string(duplicate.size())+"\r\n\r\n"+duplicate);
    web.handleClient();assert(drain(fd[1]).find("400 Bad Request")!=std::string::npos);
    assert(web.requestRejects()==2 && ingress_allocations==0);
    // Declared oversized body must be rejected without receiving any body.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    send_all(fd[1],"POST /api/settings HTTP/1.1\r\nHost: lite\r\nContent-Length: 99999\r\n\r\n");
    web.handleClient();assert(drain(fd[1]).find("413 Payload Too Large")!=std::string::npos);
    assert(web.requestRejects()==3);
    // Incomplete body followed by EOF is never dispatched.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    send_all(fd[1],"POST /api/control/arm HTTP/1.1\r\nHost: lite\r\nContent-Length: 10\r\n\r\nabc");
    web.handleClient();assert(web.dispatches==before+1);
    assert(::shutdown(fd[1],SHUT_WR)==0);
    web.handleClient();assert(drain(fd[1]).empty());
    assert(web.requestRejects()==4 && ingress_allocations==0);
    // Reject decoded NUL and invalid percent escapes before route dispatch.
    for(const auto* query:{"token=%00","token=%G0"}) {
        assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
        web.attach(fd[0],[&](){assert(false);});
        send_all(fd[1],std::string("GET /api/status?")+query+" HTTP/1.1\r\nHost: lite\r\n\r\n");
        web.handleClient();assert(drain(fd[1]).find("400 Bad Request")!=std::string::npos);
    }
    assert(web.requestRejects()==6);
    // The form allocator is bounded even when all argument names differ.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    std::string fields;
    for(size_t i=0;i<=web_argument_limit;++i)fields+=(i?"&":"")+std::to_string(i)+"=x";
    send_all(fd[1],"POST /api/settings HTTP/1.1\r\nHost: lite\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: "+
        std::to_string(fields.size())+"\r\n\r\n"+fields);
    web.handleClient();assert(drain(fd[1]).find("400 Bad Request")!=std::string::npos);
    assert(web.requestRejects()==7 && ingress_allocations==0);
    // PSRAM admission failure never falls back to internal memory.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(false);});
    request(fd[1]);
    fail_ingress_allocation=true;web.handleClient();fail_ingress_allocation=false;
    assert(drain(fd[1],true).find("503 Service Unavailable")!=std::string::npos);
    assert(web.requestRejects()==8 && ingress_allocations==0);
    // Next complete request is dispatched normally; old args do not survive.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    web.attach(fd[0],[&](){assert(web.args()==0 && web.collectedHeader()==String(""));
        assert(web.flash("recovered",9)==9);});
    request(fd[1]);web.handleClient();
    assert(drain(fd[1])=="recovered" && web.dispatches==before+2);
    assert(web.unbounded_parsers==0 && web.unbounded_finalizers==0 && ingress_allocations==0);
    // An idle speculative connection consumes no PSRAM and cannot delay
    // a complete request on another socket. Keep timeout counters honest.
    int idle[2],fast[2],partial[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,idle)==0);
    web.attach(idle[0],[&](){assert(false);});
    web.handleClient();assert(ingress_allocations==0);
    auto idle_deadline=fake_clock;
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){assert(web.header("FAST")==4);});
    request(fast[1]);web.handleClient();
    assert(drain(fast[1])=="FAST" && fake_clock-idle_deadline<100);
    assert(web.idleTimeouts()==0 && ingress_allocations==0);
    fake_clock+=web_request_budget_ms;web.handleClient();
    assert(web.idleTimeouts()==1 && drain(idle[1]).empty());
    // A partial request keeps its own buffer and can finish after a
    // second client's response. Neither route sees the other's fields.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,partial)==0);
    web.attach(partial[0],[&](){assert(false);});
    send_all(partial[1],"GET /api/slow?name=slow HTTP/1.1\r\nHost: lite\r\n");
    web.handleClient();assert(ingress_allocations==1);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){
        if(web.uri()==String("/api/slow")){
            assert(web.arg("name")==String("slow"));assert(web.header("SLOW")==4);
        } else {assert(web.uri()==String("/"));assert(web.args()==0);assert(web.header("FAST")==4);}
    });
    request(fast[1]);web.handleClient();assert(drain(fast[1])=="FAST" && ingress_allocations==1);
    send_all(partial[1],"\r\n");web.handleClient();assert(drain(partial[1])=="SLOW" && ingress_allocations==0);
    // Two idle connections cannot occupy both slots until their deadline
    // when a useful client arrives. The oldest idle connection is evicted.
    int idle2[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,idle)==0);
    web.attach(idle[0],[&](){assert(false);});web.handleClient();fake_clock+=10;
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,idle2)==0);
    web.attach(idle2[0],[&](){assert(false);});web.handleClient();
    assert(ingress_allocations==0);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){assert(web.header("FAST")==4);});request(fast[1]);web.handleClient();
    assert(web.idleEvictions()==1 && drain(idle[1]).empty() && drain(fast[1])=="FAST");
    assert(ingress_allocations==0);
    web.close();assert(drain(idle2[1]).empty());
    // Do not evict partially received requests to make room for another.
    int slow2[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,partial)==0);
    web.attach(partial[0],[&](){assert(web.header("OK")==2);});
    send_all(partial[1],"GET /a HTTP/1.1\r\nHost: lite\r\n");web.handleClient();
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,slow2)==0);
    web.attach(slow2[0],[&](){assert(web.header("OK")==2);});
    send_all(slow2[1],"GET /b HTTP/1.1\r\nHost: lite\r\n");web.handleClient();
    assert(ingress_allocations==2);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){assert(web.header("OK")==2);});
    request(fast[1]);web.handleClient();
    assert(web.idleEvictions()==1 && ingress_allocations==2);
    send_all(partial[1],"\r\n");web.handleClient();assert(drain(partial[1])=="OK");
    web.handleClient();assert(drain(fast[1])=="OK");
    send_all(slow2[1],"\r\n");web.handleClient();assert(drain(slow2[1])=="OK");
    assert(ingress_allocations==0);
    // Empty peer close is distinguishable from malformed or partial HTTP.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,idle)==0);
    web.attach(idle[0],[&](){assert(false);});close(idle[1]);web.handleClient();
    assert(web.idleCloses()==1 && web.partialTimeouts()==1 && web.partialCloses()==1);
    assert(web.resourceRejects()==1 && web.invalidRequests()==5 && ingress_allocations==0);
    // Reproduce a 508 KiB identity page on a slow connection. Each
    // iteration accepts at most 1 KiB per 200 ms (~5 KiB/s). No loop
    // waits for socket backpressure; ordinary status requests still run.
    int slow_asset[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,slow_asset)==0);
    assert(setsockopt(slow_asset[0],SOL_SOCKET,SO_SNDBUF,&buf,sizeof(buf))==0);
    std::string expected_page(508456,'x');
    for(size_t i=0;i<expected_page.size();++i)expected_page[i]=char(i%251);
    auto* owned=static_cast<uint8_t*>(heap_caps_malloc(expected_page.size(),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    assert(owned);std::memcpy(owned,expected_page.data(),expected_page.size());
    fake_clock=100;const auto asset_start=fake_clock;
    web.attach(slow_asset[0],[&](){assert(web.deferAsset("text/html",owned,expected_page.size(),owned));});
    request(slow_asset[1]);web.handleClient();
    assert(web.assetBusy() && ingress_allocations==1);
    // A concurrent asset is rejected, and its owned memory is released.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){
        auto* extra=static_cast<uint8_t*>(heap_caps_malloc(2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        assert(extra);extra[0]=extra[1]=0;assert(!web.deferAsset("text/html",extra,2,extra));
    });
    request(fast[1]);web.handleClient();assert(drain(fast[1]).find("503 Response")!=std::string::npos);
    assert(web.assetBusy() && ingress_allocations==1);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fast)==0);
    web.attach(fast[0],[&](){assert(web.header("STATUS")==6);});
    request(fast[1]);web.handleClient();assert(drain(fast[1])=="STATUS");
    std::string received;
    bool done=false;
    for(int turns=0;turns<800 && !done;++turns) {
        fake_clock+=200;const auto tick=fake_clock;
        web.handleClient();
        assert(fake_clock-tick<=1); // only the existing idle-loop delay
        char bytes[1024];const auto n=recv(slow_asset[1],bytes,sizeof(bytes),MSG_DONTWAIT);
        if(n>0)received.append(bytes,size_t(n));
        else if(n==0)done=true;
        else assert(errno==EAGAIN || errno==EWOULDBLOCK);
    }
    assert(done && !web.assetBusy() && ingress_allocations==0);
    assert(fake_clock-asset_start>web_response_budget_ms);
    const auto body_at=received.find("\r\n\r\n");
    assert(body_at!=std::string::npos && received.substr(body_at+4)==expected_page);
    assert(received.find("Content-Length: 508456\r\n")!=std::string::npos);
    close(slow_asset[1]);
    // A stalled asset is still finite and cannot keep PSRAM or the only
    // transfer slot forever. Closing the server also releases ownership.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,slow_asset)==0);
    assert(setsockopt(slow_asset[0],SOL_SOCKET,SO_SNDBUF,&buf,sizeof(buf))==0);
    const auto abort_before=web.responseAborts();
    web.attach(slow_asset[0],[&](){
        auto* extra=static_cast<uint8_t*>(heap_caps_malloc(expected_page.size(),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        assert(extra);std::memcpy(extra,expected_page.data(),expected_page.size());
        assert(web.deferAsset("text/html",extra,expected_page.size(),extra));
    });
    request(slow_asset[1]);web.handleClient();web.handleClient();
    fake_clock+=web_asset_idle_ms;web.handleClient();
    assert(!web.assetBusy() && ingress_allocations==0 && web.responseAborts()==abort_before+1);
    assert(drain(slow_asset[1]).size()<expected_page.size());
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,slow_asset)==0);
    web.attach(slow_asset[0],[&](){
        auto* extra=static_cast<uint8_t*>(heap_caps_malloc(8,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        assert(extra);std::memset(extra,0,8);assert(web.deferAsset("text/html",extra,8,extra));
    });
    request(slow_asset[1]);web.handleClient();assert(web.assetBusy());
    web.close();assert(!web.assetBusy() && ingress_allocations==0);
    assert(drain(slow_asset[1]).find("Content-Length: 8")!=std::string::npos);
    std::cout<<"PASS actual CottageWebServer ingress and output: fairness, preconnect, bounded partial request, timeout categories, recovery and chunk framing\n";
}
