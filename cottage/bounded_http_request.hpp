#pragma once
// Independent bounded HTTP/1.0 and HTTP/1.1 ingress for the local UI.
// No socket waiting, dynamic allocation or application dispatch in this codec.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace enevina::cottage {
inline constexpr size_t web_header_limit=8192,web_body_limit=8192;
inline constexpr size_t web_request_capacity=web_header_limit+web_body_limit;
inline constexpr size_t web_target_limit=2048,web_argument_limit=64;
inline constexpr size_t web_receive_block=4096;
inline constexpr uint32_t web_request_budget_ms=3000;
// Two incremental ingress slots permit a useful request to pass a slow
// connection. Idle speculative sockets can be evicted; partial requests
// retain the original absolute deadline and are never evicted for fairness.
inline constexpr size_t web_pending_connections=2;
enum class HttpInput : uint8_t { Pending, Complete, BadRequest, TooLarge, Unsupported };
enum class HttpVerb : uint8_t { Get, Post };
enum class HttpBody : uint8_t { None, Form, Plain };
struct HttpHead {
    HttpVerb verb=HttpVerb::Get;
    HttpBody body=HttpBody::None;
    unsigned minor=1;
    std::string_view path{},query{},host{},headers{};
    size_t length=0;
};
inline bool iequal(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
inline std::string_view ows(std::string_view v) {
    while(!v.empty()&&(v.front()==' '||v.front()=='\t'))v.remove_prefix(1);
    while(!v.empty()&&(v.back()==' '||v.back()=='\t'))v.remove_suffix(1);
    return v;
}
inline bool token_char(unsigned char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
           std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos;
}
inline bool split_header(std::string_view line,std::string_view& key,std::string_view& value) {
    const auto colon=line.find(':');
    if(colon==0||colon==std::string_view::npos)return false;
    key=line.substr(0,colon);value=ows(line.substr(colon+1));
    for(unsigned char c:key)if(!token_char(c))return false;
    for(unsigned char c:value)if((c<32&&c!='\t')||c==127)return false;
    return true;
}
// Accept explicit gzip with a positive RFC qvalue. Unknown/malformed
// quality parameters are not opt-in. Explicit refusal beats a wildcard.
inline bool positive_encoding_quality(std::string_view params) {
    if(params.empty())return true;
    params=ows(params);
    const auto eq=params.find('=');
    if(eq==std::string_view::npos || !iequal(ows(params.substr(0,eq)),"q"))return false;
    const auto q=ows(params.substr(eq+1));
    if(q.empty() || (q[0]!='0'&&q[0]!='1') || q.size()>5)return false;
    if(q.size()==1)return q[0]=='1';
    if(q[1]!='.')return false;
    bool positive=q[0]=='1';
    for(size_t i=2;i<q.size();++i) {
        if(q[i]<'0'||q[i]>'9'||(q[0]=='1'&&q[i]!='0'))return false;
        if(q[i]!='0')positive=true;
    }
    return positive;
}
inline bool accepts_gzip(std::string_view headers) {
    bool explicit_gzip=false,gzip_allowed=true,wildcard=false,wildcard_allowed=true;
    while(!headers.empty()) {
        const auto end=headers.find("\r\n");
        std::string_view key,value;
        if(!split_header(headers.substr(0,end),key,value))return false;
        if(iequal(key,"Accept-Encoding")) {
            while(!value.empty()) {
                const auto comma=value.find(',');
                const auto item=ows(value.substr(0,comma));
                const auto semi=item.find(';');
                const auto coding=ows(item.substr(0,semi));
                const bool positive=semi==std::string_view::npos ||
                    positive_encoding_quality(item.substr(semi+1));
                if(iequal(coding,"gzip")) {explicit_gzip=true;gzip_allowed&=positive;}
                if(coding=="*"){wildcard=true;wildcard_allowed&=positive;}
                if(comma==std::string_view::npos)break;
                value.remove_prefix(comma+1);
            }
        }
        if(end==std::string_view::npos)break;
        headers.remove_prefix(end+2);
    }
    return explicit_gzip?gzip_allowed:(wildcard&&wildcard_allowed);
}
inline HttpInput parse_http_head(std::string_view bytes,HttpHead& head) {
    head={};
    if(bytes.size()>web_header_limit)return HttpInput::TooLarge;
    if(bytes.size()<4||bytes.substr(bytes.size()-4)!="\r\n\r\n")return HttpInput::BadRequest;
    const auto line_end=bytes.find("\r\n");
    const auto line=bytes.substr(0,line_end);
    const auto first=line.find(' ');
    const auto second=first==std::string_view::npos?first:line.find(' ',first+1);
    if(first==std::string_view::npos||second==std::string_view::npos||second==first+1)
        return HttpInput::BadRequest;
    const auto method=line.substr(0,first);
    if(method=="GET")head.verb=HttpVerb::Get;
    else if(method=="POST")head.verb=HttpVerb::Post;
    else return HttpInput::Unsupported;
    const auto target=line.substr(first+1,second-first-1);
    if(target.size()>web_target_limit)return HttpInput::TooLarge;
    if(target.empty()||target.front()!='/')return HttpInput::BadRequest;
    for(unsigned char c:target)if(c<=32||c==127||c=='#')return HttpInput::BadRequest;
    const auto version=line.substr(second+1);
    if(version=="HTTP/1.0")head.minor=0;
    else if(version!="HTTP/1.1")return HttpInput::BadRequest;
    const auto query=target.find('?');
    head.path=target.substr(0,query);
    if(query!=std::string_view::npos)head.query=target.substr(query+1);
    head.headers=bytes.substr(line_end+2,bytes.size()-line_end-4);
    bool has_host=false,has_length=false,has_type=false;
    std::string_view type{};
    size_t count=0;
    auto rest=head.headers;
    while(!rest.empty()) {
        const auto end=rest.find("\r\n");
        if(end==std::string_view::npos||end==0)return HttpInput::BadRequest;
        if(++count>64)return HttpInput::TooLarge;
        std::string_view key,value;
        if(!split_header(rest.substr(0,end),key,value))return HttpInput::BadRequest;
        rest.remove_prefix(end+2);
        if(iequal(key,"Host")) {
            if(has_host||value.empty())return HttpInput::BadRequest;
            has_host=true;head.host=value;
        } else if(iequal(key,"Content-Length")) {
            if(has_length||value.empty())return HttpInput::BadRequest;
            has_length=true;
            for(unsigned char c:value) {
                if(c<'0'||c>'9')return HttpInput::BadRequest;
                const size_t digit=c-'0';
                if(head.length>(web_body_limit-digit)/10)return HttpInput::TooLarge;
                head.length=head.length*10+digit;
            }
        } else if(iequal(key,"Transfer-Encoding")||iequal(key,"Expect")) {
            // The UI never sends chunked requests or 100-continue.
            // Do not accept ambiguous framing or wait for another handshake.
            return HttpInput::Unsupported;
        } else if(iequal(key,"Content-Type")) {
            if(has_type)return HttpInput::BadRequest;
            has_type=true;type=ows(value.substr(0,value.find(';')));
        }
    }
    if(head.minor&&!has_host)return HttpInput::BadRequest;
    if(head.verb==HttpVerb::Get&&head.length)return HttpInput::BadRequest;
    if(head.length) {
        if(iequal(type,"application/x-www-form-urlencoded"))head.body=HttpBody::Form;
        else if(type.empty()||iequal(type,"text/plain")||iequal(type,"application/json"))
            head.body=HttpBody::Plain;
        else return HttpInput::Unsupported;
    }
    return HttpInput::Complete;
}

// The caller owns a fixed-capacity buffer for exactly one live request.
// Header delimiter scanning resumes at the previous boundary, even when
// the peer sends a single byte per poll. The body is never searched as headers.
class HttpIngress {
public:
    void reset(){used_=scanned_=header_end_=0;head_={};state_=HttpInput::Pending;}
    HttpInput state()const{return state_;}
    size_t used()const{return used_;}
    size_t room()const{return web_request_capacity-used_;}
    const HttpHead& head()const{return head_;}
    std::string_view body(const char* buffer)const {
        return state_==HttpInput::Complete?std::string_view(buffer+header_end_,head_.length):std::string_view{};
    }
    HttpInput received(const char* buffer,size_t n) {
        if(state_!=HttpInput::Pending)return state_;
        if(!buffer||n>room())return state_=HttpInput::TooLarge;
        used_+=n;
        if(!header_end_) {
            const std::string_view bytes(buffer,used_);
            const auto end=bytes.find("\r\n\r\n",scanned_);
            if(end==std::string_view::npos) {
                if(used_>=web_header_limit)return state_=HttpInput::TooLarge;
                scanned_=used_>3?used_-3:0;
                return state_;
            }
            header_end_=end+4;
            const auto parsed=parse_http_head(bytes.substr(0,header_end_),head_);
            if(parsed!=HttpInput::Complete)return state_=parsed;
        }
        const size_t expected=header_end_+head_.length;
        if(used_>expected)return state_=HttpInput::BadRequest; // no pipelining
        if(used_==expected)state_=HttpInput::Complete;
        return state_;
    }
private:
    size_t used_=0,scanned_=0,header_end_=0;
    HttpHead head_{};
    HttpInput state_=HttpInput::Pending;
};
inline bool request_expired(uint32_t now,uint32_t started) {
    return uint32_t(now-started)>=web_request_budget_ms;
}
inline int form_hex(unsigned char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
// Apply URL/form decoding once, rejecting malformed escapes and NUL.
// UTF-8 bytes are preserved, as are legal encoded SSID/password characters.
template<class Append>
bool decode_http_form(std::string_view input,Append append) {
    for(size_t i=0;i<input.size();++i) {
        unsigned char c=input[i];
        if(c=='+')c=' ';
        else if(c=='%') {
            if(input.size()-i<3)return false;
            const int a=form_hex(input[i+1]),b=form_hex(input[i+2]);
            if(a<0||b<0)return false;
            c=static_cast<unsigned char>((a<<4)|b);i+=2;
        }
        if(!c||!append(static_cast<char>(c)))return false;
    }
    return true;
}
template<class Field>
bool each_http_field(std::string_view fields,Field field) {
    while(!fields.empty()) {
        const auto amp=fields.find('&');
        const auto pair=fields.substr(0,amp);
        if(!pair.empty()) {
            const auto eq=pair.find('=');
            const auto key=pair.substr(0,eq);
            const auto value=eq==std::string_view::npos?std::string_view{}:pair.substr(eq+1);
            if(key.empty()||!field(key,value))return false;
        }
        if(amp==std::string_view::npos)break;
        fields.remove_prefix(amp+1);
    }
    return true;
}
} // namespace enevina::cottage
