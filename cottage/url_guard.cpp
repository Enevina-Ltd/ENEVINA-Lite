#include "cottage/url_guard.hpp"
#include <cstddef>
#include <cstring>
namespace enevina::cottage {
namespace {
bool alnum(char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');
}
bool safe_authority(const char* begin, size_t size) {
    if(!begin||size==0||size>100) return false;
    bool previous_dot=true, seen_port=false;
    unsigned port=0,port_digits=0;
    for(size_t i=0;i<size;i++) {
        const char c=begin[i];
        if(c==':') {
            if(seen_port||i==0||i==size-1||previous_dot)return false;
            seen_port=true;previous_dot=false;continue;
        }
        if(seen_port) {
            if(c<'0'||c>'9'||++port_digits>5)return false;
            port=port*10+static_cast<unsigned>(c-'0');
            continue;
        }
        if(!(alnum(c)||c=='-'||c=='.'))return false;
        if(c=='.') {
            if(previous_dot)return false;
            previous_dot=true;
        } else previous_dot=false;
    }
    return !previous_dot&&(!seen_port||(port_digits>0&&port>0&&port<=65535));
}
bool approved(const char* url,const char* required_path) {
    if(!url)return false;
    constexpr char scheme[]="http://";
    if(std::strncmp(url,scheme,sizeof(scheme)-1)!=0)return false;
    const size_t n=std::strlen(url);
    if(n<sizeof(scheme)||n>150)return false;
    const char* authority=url+sizeof(scheme)-1;
    const char* slash=std::strchr(authority,'/');
    const size_t length=slash?static_cast<size_t>(slash-authority):std::strlen(authority);
    if(!safe_authority(authority,length))return false;
    if(required_path)return slash&&std::strcmp(slash,required_path)==0;
    return !slash; // Shelly accepts only scheme://host[:port]
}
}
bool valid_cn105_events_url(const char* url) {
    return approved(url,"/events");
}
bool valid_shelly_base_url(const char* url) {
    return approved(url,nullptr);
}
}
