#include "cottage/bounded_http_request.hpp"
#include <cassert>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
using namespace enevina::cottage;
static HttpInput parse(std::string_view req) {
    std::array<char,web_request_capacity> bytes{};
    HttpIngress in;
    if(req.size()>bytes.size())return HttpInput::TooLarge;
    std::memcpy(bytes.data(),req.data(),req.size());
    return in.received(bytes.data(),req.size());
}
int main(){
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\n\r\n")==HttpInput::Complete);
    assert(parse("GET /generate_204 HTTP/1.0\r\n\r\n")==HttpInput::Complete);
    assert(parse("GET / HTTP/1.1\r\n\r\n")==HttpInput::BadRequest);
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\n")==HttpInput::Pending);
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\nHost: other\r\n\r\n")==HttpInput::BadRequest);
    assert(parse("GET / HTTP/1.1\r\nHost : lite\r\n\r\n")==HttpInput::BadRequest);
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\n folded\r\n\r\n")==HttpInput::BadRequest);
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\nContent-Length: 1\r\n\r\nx")==HttpInput::BadRequest);
    assert(parse("GET / HTTP/1.1\r\nHost: lite\r\n\r\nextra")==HttpInput::BadRequest);
    assert(parse("DELETE /api/settings HTTP/1.1\r\nHost: lite\r\n\r\n")==HttpInput::Unsupported);
    assert(!accepts_gzip("Host: lite\r\n"));
    assert(accepts_gzip("Host: lite\r\nAccept-Encoding: br, gzip, deflate\r\n"));
    assert(accepts_gzip("accept-encoding: GZIP ; q=0.125\r\n"));
    assert(accepts_gzip("Accept-Encoding: gzip;q=1.000\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip;q=0, *;q=1\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip;q=0.000\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip;q=1.1\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip;q=0.0001\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip;q=oops\r\n"));
    assert(!accepts_gzip("Accept-Encoding: x-gzip\r\n"));
    assert(accepts_gzip("Accept-Encoding: *;q=0.2\r\n"));
    assert(!accepts_gzip("Accept-Encoding: gzip\r\nAccept-Encoding: gzip;q=0\r\n"));
    const std::string start="POST /api/settings HTTP/1.1\r\nHost: lite\r\n";
    assert(parse(start+"Content-Length: 1\r\ncontent-length: 1\r\n\r\nx")==HttpInput::BadRequest);
    assert(parse(start+"Content-Length: -1\r\n\r\n")==HttpInput::BadRequest);
    assert(parse(start+"Content-Length: 1, 1\r\n\r\nx")==HttpInput::BadRequest);
    assert(parse(start+"Content-Length: 18446744073709551616\r\n\r\n")==HttpInput::TooLarge);
    assert(parse(start+"Transfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n")==HttpInput::Unsupported);
    assert(parse(start+"Expect: 100-continue\r\nContent-Length: 1\r\n\r\n")==HttpInput::Unsupported);
    assert(parse(start+"Content-Type: multipart/form-data; boundary=x\r\nContent-Length: 1\r\n\r\nx")==HttpInput::Unsupported);
    assert(parse(start+"Content-Length: 4\r\n\r\nabc")==HttpInput::Pending);
    assert(parse(start+"Content-Length: 4\r\n\r\nabcde")==HttpInput::BadRequest);
    assert(parse(start+"Content-Length: 4\r\n\r\nabcd")==HttpInput::Complete);
    assert(parse(start+"Content-Length: 8192\r\n\r\n"+std::string(8192,'x'))==HttpInput::Complete);
    assert(parse(start+"Content-Length: 8193\r\n\r\n")==HttpInput::TooLarge);
    assert(parse("GET /"+std::string(web_target_limit,'a')+" HTTP/1.1\r\nHost: lite\r\n\r\n")==HttpInput::TooLarge);
    assert(parse(std::string(web_header_limit,'x'))==HttpInput::TooLarge);
    std::array<char,web_request_capacity> buf{};
    const std::string req="POST /api/wifi/connect?draft=true HTTP/1.1\r\nHost: lite\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\nssid=A%2BB!";
    HttpIngress in;
    for(size_t i=0;i<req.size();++i){
        buf[i]=req[i];
        const auto s=in.received(buf.data(),1);
        assert(s==(i+1==req.size()?HttpInput::Complete:HttpInput::Pending));
    }
    assert(in.head().path=="/api/wifi/connect" && in.head().query=="draft=true");
    assert(in.head().body==HttpBody::Form && in.body(buf.data())=="ssid=A%2BB!");
    std::string value;auto append=[&](char c){value+=c;return true;};
    assert(decode_http_form("A%2BB+%C3%A4",append) && value=="A+B ä");
    for(auto bad:{"%","%2","%GG","%00"})assert(!decode_http_form(bad,append));
    size_t fields=0;
    assert(each_http_field("x=1&y=2&flag",[&](auto,auto){++fields;return true;})&&fields==3);
    assert(!each_http_field("=1",[](auto,auto){return true;}));
    assert(!request_expired(UINT32_MAX-2,UINT32_MAX-10));
    assert(request_expired(uint32_t(UINT32_MAX-10+web_request_budget_ms),UINT32_MAX-10));
    std::cout<<"PASS bounded HTTP ingress: strict framing, size limits, bytewise fragmentation, decoding and wraparound\n";
}
