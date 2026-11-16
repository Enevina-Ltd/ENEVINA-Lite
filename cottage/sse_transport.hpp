#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage {

// HTTPClient::getStreamPtr() exposes transport bytes, not a decoded
// Transfer-Encoding: chunked body. Decode byte-by-byte with bounded state.
// Returns false on a framing error. 'produced' is true for body bytes only.
class HttpChunkDecoder {
public:
    bool feed(uint8_t input, uint8_t& body, bool& produced) {
        produced=false;
        switch(state_) {
        case State::Size:
            if(input==';') {
                if(!digits_)return fail();
                state_=State::Extension;
            } else if(input=='\r') {
                if(!digits_)return fail();
                state_=State::SizeLF;
            } else {
                unsigned value;
                if(input>='0' && input<='9') value=input-'0';
                else if(input>='a' && input<='f') value=input-'a'+10;
                else if(input>='A' && input<='F') value=input-'A'+10;
                else return fail();
                if(++digits_>8 || length_>(max_chunk-value)/16)return fail();
                length_=length_*16+value;
                if(length_>max_chunk)return fail();
            }
            return true;
        case State::Extension:
            if(input=='\r') state_=State::SizeLF;
            else if(input<' ' || ++extension_bytes_>96)return fail();
            return true;
        case State::SizeLF:
            if(input!='\n')return fail();
            state_=length_==0?State::Done:State::Body;
            return true;
        case State::Body:
            body=input;produced=true;
            if(--length_==0)state_=State::DataCR;
            return true;
        case State::DataCR:
            if(input!='\r')return fail();
            state_=State::DataLF;
            return true;
        case State::DataLF:
            if(input!='\n')return fail();
            length_=0;digits_=0;extension_bytes_=0;state_=State::Size;
            return true;
        case State::Done: return true; // trailers ignored; reconnect on EOF
        case State::Invalid: return false;
        }
        return false;
    }
    bool finished() const {return state_==State::Done;}
    bool failed() const {return state_==State::Invalid;}
private:
    static constexpr uint32_t max_chunk=262144;
    enum class State:uint8_t {Size,Extension,SizeLF,Body,DataCR,DataLF,Done,Invalid};
    State state_=State::Size;
    uint32_t length_=0;
    unsigned digits_=0;
    unsigned extension_bytes_=0;
    bool fail() {state_=State::Invalid;return false;}
};

// Incremental SSE records: field lines can be split at arbitrary TCP and
// HTTP chunk boundaries. Only complete "event: state" JSON is dispatched.
// Both line and aggregate JSON buffers are bounded; oversize events fail closed.
class SseStateParser {
public:
    using Callback=void (*)(const char* json,void* context);
    explicit SseStateParser(Callback cb,void* context):cb_(cb),ctx_(context){}
    void feed(uint8_t byte) {
        if(byte=='\r')return;
        if(byte=='\n') {
            const bool blank=line_size_==0;
            if(invalid_) {
                // Wait for the end of the rejected SSE event, not merely
                // the end of a truncated data line.
                if(blank) reset();
            } else {
                line_[line_size_]=0;
                consume_line();
            }
            line_size_=0;
            return;
        }
        if(line_size_<sizeof(line_)-1 && !invalid_) {
            line_[line_size_++]=static_cast<char>(byte);

        } else invalid_=true;

    }
    unsigned rejected() const {return rejected_;}
private:
    char line_[1536]{};
    char payload_[3072]{};
    std::size_t line_size_=0,payload_size_=0;
    bool state_event_=false,invalid_=false;
    unsigned rejected_=0;
    Callback cb_;
    void* ctx_;
    void reset() {
        payload_size_=0;state_event_=false;invalid_=false;
    }
    void consume_line() {
        if(line_size_==0) {
            if(state_event_ && payload_size_ && cb_) {
                payload_[payload_size_]=0;
                cb_(payload_,ctx_);
            }
            reset();
            return;
        }
        if(std::strncmp(line_,"event:",6)==0) {
            const char* value=line_+6;
            if(*value==' ')++value;
            state_event_=std::strcmp(value,"state")==0;
        } else if(std::strncmp(line_,"data:",5)==0) {
            const char* value=line_+5;
            if(*value==' ')++value;
            const size_t n=std::strlen(value);
            if(n+payload_size_+2>sizeof(payload_)) {
                invalid_=true;++rejected_;return;
            }
            if(payload_size_)payload_[payload_size_++]='\n';
            std::memcpy(payload_+payload_size_,value,n);
            payload_size_+=n;
        }
    }
};
} // namespace enevina::cottage
