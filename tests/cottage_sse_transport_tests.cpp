#include "cottage/sse_transport.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace enevina::cottage;
static void on_event(const char* json,void* ptr) {
    auto* out=static_cast<std::vector<std::string>*>(ptr);
    out->push_back(json);
}
static std::string chunks(const std::string& body,size_t each) {
    std::string output;
    for(size_t pos=0;pos<body.size();pos+=each) {
        const size_t n=std::min(each,body.size()-pos);
        char length[32];
        std::snprintf(length,sizeof(length),"%zx\r\n",n);
        output+=length;
        output.append(body,pos,n);
        output+="\r\n";
    }
    output+="0\r\n\r\n";
    return output;
}
static std::vector<std::string> parse(const std::string& raw,bool chunked) {
    std::vector<std::string> result;
    HttpChunkDecoder decode;
    SseStateParser events(on_event,&result);
    for(unsigned char b:raw) {
        uint8_t payload=0;
        bool produced=!chunked;
        if(chunked)assert(decode.feed(b,payload,produced));
        if(produced) events.feed(chunked?payload:b);
    }
    if(chunked)assert(decode.finished());
    return result;
}
int main() {
    const std::string state=R"({"id":"climate/Mitsubishi Electric MSZ-FD25VA","mode":"OFF","current_temperature":"12.0","target_temperature":"21.0"})";
    const std::string freq=R"({"id":"sensor/Compressor Frequency","value":0})";
    const std::string feed="retry: 30000\r\n\r\n"
        "event: state\r\n"
        "data: "+state+"\r\n\r\n"
        "event: ping\r\n"
        "data: {}\r\n\r\n"
        "event: state\r\n"
        "data: "+freq+"\r\n\r\n";
    for(size_t size:{1u,2u,3u,4u,7u,16u,51u,511u}) {
        const auto observed=parse(chunks(feed,size),true);
        assert(observed.size()==2);
        assert(observed[0]==state && observed[1]==freq);
    }
    const auto plain=parse(feed,false);
    assert(plain.size()==2 && plain[0]==state && plain[1]==freq);
    // Partial event must not be emitted without blank-line termination.
    std::vector<std::string> incomplete;
    SseStateParser parser(on_event,&incomplete);
    for(unsigned char c:std::string("event: state\n")+("data: "+state))
        parser.feed(c);
    assert(incomplete.empty());
    for(unsigned char c:std::string("\n\n"))parser.feed(c);
    assert(incomplete.size()==1 && incomplete[0]==state);
    // Oversized data cannot be spliced into another event.
    std::vector<std::string> rejected;
    SseStateParser over(on_event,&rejected);
    const std::string bad="event: state\n"+std::string("data: ")+
        std::string(4000,'X')+"\n\n";
    for(unsigned char c:bad)over.feed(c);
    assert(rejected.empty());
    for(unsigned char c:std::string("event: state\ndata: "+freq+"\n\n"))
        over.feed(c);
    assert(rejected.size()==1 && rejected[0]==freq);

    // Embedded NUL must never truncate a state JSON object into valid data.
    // It is malformed framing even if the preceding prefix is valid JSON.
    const std::string injection="event: state\n" + ("data: "+state) +
        std::string(1,'\0') + "malformed\n\n";
    assert(parse(injection,false).empty());
    assert(parse(chunks(injection,2),true).empty());
    const std::string recovered_frame=injection+"event: state\ndata: "+freq+"\n\n";
    const auto recovered=parse(recovered_frame,false);
    assert(recovered.size()==1 && recovered[0]==freq);

    // Escaped U+0000 also becomes a C-string terminator after cJSON decode.
    const std::string unicode_nul=R"({"id":"probe","value":"ok\u0000bad"})";
    const std::string unicode_injection="event: state\ndata: "+unicode_nul+"\n\n";
    assert(parse(unicode_injection,false).empty());
    assert(parse(chunks(unicode_injection,7),true).empty());
    // The literal characters \u0000 (escaped backslash) are harmless.
    const std::string literal_escape=R"({"id":"probe","value":"ok\\u0000"})";
    const auto literal_events=parse("event: state\ndata: "+literal_escape+"\n\n",false);
    assert(literal_events.size()==1 && literal_events[0]==literal_escape);
    // Invalid hex framing and impossible chunk lengths fail closed.
    HttpChunkDecoder malformed;
    uint8_t b=0;bool emitted=false;
    assert(!malformed.feed('G',b,emitted));
    assert(malformed.failed());
    HttpChunkDecoder oversize;
    for(unsigned char c:std::string("40001")) {
        if(!oversize.feed(c,b,emitted))break;
    }
    assert(oversize.failed());
    std::puts("PASS SSE raw/chunked, arbitrary splits, incomplete frames and oversize recovery");
}
