#pragma once

#include <cstddef>
#include <cstdint>

namespace enevina_cn105 {

struct Snapshot {
    uint32_t epoch=0, sequence=0;
    int64_t oldest_utc=0, latest_utc=0;
    int indoor_half_c=0, target_half_c=0;
    unsigned compressor_hz=0;
    const char* mode="OTHER";
};

// Observes the existing UART driver. It never sends or schedules a packet.
class VerifiedPoll {
public:
    void set_epoch(uint32_t epoch) {
        if(last_.epoch==0)last_.epoch=epoch;
    }
    void begin() {
        staging_={};
        next_=0x02;
        awaiting_=false;
    }
    void discard() { next_=0; awaiting_=false; }
    bool active() const { return next_!=0; }
    void sent(uint8_t code,uint64_t now_ms) {
        if(next_==0)return;
        if(awaiting_ || code!=next_) { discard(); return; }
        sent_ms_=now_ms;
        awaiting_=true;
    }
    bool receive(const uint8_t* frame,std::size_t size,uint64_t now_ms,int64_t utc) {
        if(next_==0)return false;
        if(!awaiting_ || !valid_response(frame,size) || frame[5]!=next_ ||
           now_ms<sent_ms_ || now_ms-sent_ms_>60000 ||
           utc<1700000000 || utc<last_.latest_utc || utc<staging_.latest_utc) {
            discard();
            return false;
        }
        awaiting_=false;
        const uint8_t* data=frame+5;
        if(next_==0x02) {
            if(!read_settings(data)) { discard(); return false; }
            staging_.oldest_utc=utc;
            first_ms_=now_ms;
            next_=0x03;
        } else if(next_==0x03) {
            if(!read_room(data)) { discard(); return false; }
            next_=0x06;
        } else {
            discard();
            if(last_.epoch==0 || last_.sequence==UINT32_MAX ||
               now_ms<first_ms_ || now_ms-first_ms_>60000 ||
               utc-staging_.oldest_utc>60 || !read_status(data))return false;
            staging_.latest_utc=utc;
            staging_.epoch=last_.epoch;
            staging_.sequence=last_.sequence+1;
            last_=staging_;
            return true;
        }
        staging_.latest_utc=utc;
        return false;
    }
    const Snapshot& snapshot() const { return last_; }

private:
    static bool valid_response(const uint8_t* frame,std::size_t size) {
        if(!frame || size!=22 || frame[0]!=0xfc || frame[1]!=0x62 ||
           frame[2]!=1 || frame[3]!=0x30 || frame[4]!=16)return false;
        uint8_t sum=0;
        for(std::size_t i=0;i<size;++i)sum+=frame[i];
        return sum==0xfc;
    }
    bool read_settings(const uint8_t* data) {
        // Same standard and i-See mode encodings as the pinned CN105 driver.
        // Reject unknown fields instead of using its cached fallbacks.
        const auto mode=data[4]>8?data[4]-8:data[4];
        if(data[3]>1 || (mode!=1 && mode!=2 && mode!=3 && mode!=7 && mode!=8))
            return false;
        if(data[11]==0 && data[5]>15)return false;
        staging_.target_half_c=data[11]?int(data[11])-128:2*(31-data[5]);
        if(staging_.target_half_c<0 || staging_.target_half_c>70)return false;
        staging_.mode=data[3]==0?"OFF":mode==1?"HEAT":"OTHER";
        return true;
    }
    bool read_room(const uint8_t* data) {
        if(data[6]==0 && data[3]>31)return false;
        staging_.indoor_half_c=data[6]?int(data[6])-128:2*(10+data[3]);
        return staging_.indoor_half_c>=-20 && staging_.indoor_half_c<=100;
    }
    bool read_status(const uint8_t* data) {
        if(data[4]>1 || data[3]>200)return false;
        // The status packet identifies standby separately from frequency.
        staging_.compressor_hz=data[4]?data[3]:0;
        return true;
    }
    Snapshot staging_{},last_{};
    uint64_t sent_ms_=0,first_ms_=0;
    uint8_t next_=0;
    bool awaiting_=false;
};

} // namespace enevina_cn105
