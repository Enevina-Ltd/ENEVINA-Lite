#pragma once
// Flash persistence for the history ring and the event log through an abstract
// file backend (LittleFS in the firmware, memory in the tests).
//
// Wear and safety rules
//  * Records are APPENDED in batches (the caller flushes about once an hour);
//    the file is rewritten (backend.replace, which must be atomic) only to
//    compact after kHistoryCompactSlack extra records or to repair a bad tail.
//  * Every record carries a CRC-16. A record that fails its CRC, repeats a slot
//    or goes backwards ends the load: the valid prefix is kept, the file is
//    marked for repair and rewritten on the next flush.
//  * A failed append never advances the persisted position; the next flush
//    repairs the file by compaction instead of appending after a torn write.
//  * A header with another magic/version/record size is treated as an empty
//    file and replaced on the next flush, never "interpreted".
// Little-endian layout (ESP32 and x86 hosts); the version field guards changes.
#include "cottage/history_store.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace enevina::cottage {
// A failed mount does NOT prove the volume is blank. Only permit the initial
// format after reading every byte of the target flash partition as erased.
// A read error or any non-FF byte preserves the partition for recovery.
template<class Read>
inline bool history_partition_is_blank(size_t length,Read read) {
    if(!length)return false;
    uint8_t buf[512];
    for(size_t off=0;off<length;) {
        const size_t n=std::min(sizeof(buf),length-off);
        if(!read(off,buf,n))return false;
        for(size_t j=0;j<n;++j)if(buf[j]!=0xFF)return false;
        off+=n;
    }
    return true;
}
class FileBackend {
public:
    virtual ~FileBackend()=default;
    virtual bool exists()=0;
    virtual size_t size()=0;
    virtual bool read(size_t offset,void* out,size_t n)=0;
    virtual bool append(const void* data,size_t n)=0;
    // Atomic whole-file replace, streamed so no buffer the size of the file is
    // ever needed (the firmware has little contiguous internal RAM): nothing is
    // visible until replace_commit() succeeds; replace_abort() drops the temp.
    virtual bool replace_begin()=0;
    virtual bool replace_write(const void* data,size_t n)=0;
    virtual bool replace_commit()=0;
    virtual void replace_abort()=0;
};

inline uint16_t history_crc16(const uint8_t* d,size_t n) {
    uint16_t crc=0xFFFF;
    for(size_t i=0;i<n;++i) {
        crc^=static_cast<uint16_t>(d[i])<<8;
        for(int b=0;b<8;++b) crc=(crc&0x8000)?static_cast<uint16_t>((crc<<1)^0x1021):static_cast<uint16_t>(crc<<1);
    }
    return crc;
}

namespace persist_detail {
constexpr uint32_t kRecMagic=0x53484E45;   // "ENHS"
constexpr uint32_t kEvtMagic=0x45484E45;   // "ENHE"
constexpr uint16_t kVersion=1;
struct Header {
    uint32_t magic=0;
    uint16_t version=kVersion;
    uint16_t record_size=0;
};
static_assert(sizeof(Header)==8,"header layout");
constexpr size_t kRecDisk=sizeof(HistoryRecord)+2;
constexpr size_t kEvtDisk=sizeof(HistoryEvent)+2;
// First released history image stored events WITHOUT the 32-bit OTA arg.
// Keep this exact disk layout decode-only to preserve existing outage logs.
struct LegacyHistoryEvent {uint32_t utc;uint8_t kind;uint8_t source;int16_t value;};
static_assert(sizeof(LegacyHistoryEvent)==8,"legacy event ABI changed");
constexpr size_t kLegacyEvtDisk=sizeof(LegacyHistoryEvent)+2;

template<class T>
inline void encode(const T& v,uint8_t* out) {
    std::memcpy(out,&v,sizeof(T));
    const uint16_t crc=history_crc16(out,sizeof(T));
    std::memcpy(out+sizeof(T),&crc,2);
}
template<class T>
inline bool decode(const uint8_t* in,T& v) {
    uint16_t crc=0;
    std::memcpy(&crc,in+sizeof(T),2);
    if(crc!=history_crc16(in,sizeof(T))) return false;
    std::memcpy(&v,in,sizeof(T));
    return true;
}
inline Header header(uint32_t magic,size_t record_disk) {
    Header h;h.magic=magic;h.record_size=static_cast<uint16_t>(record_disk);return h;
}
} // namespace persist_detail

struct LoadResult {
    size_t records=0,events=0;
    bool record_header_ok=false,event_header_ok=false;
    bool record_tail_cut=false,event_tail_cut=false;
};

class HistoryPersistence {
public:
    HistoryPersistence(FileBackend& records,FileBackend& events):rec_(records),evt_(events) {}

    LoadResult load(HistoryStore& store,EventLog& events) {
        using namespace persist_detail;
        LoadResult r;
        // ---- records ----
        rec_dirty_=false;file_records_=0;persisted_slot_=0;
        if(rec_.exists()&&header_ok(rec_,kRecMagic,kRecDisk)) {
            r.record_header_ok=true;
            uint32_t prev=0;
            const bool clean=scan(rec_,kRecDisk,[&](const uint8_t* p)->bool {
                HistoryRecord rec;
                if(!decode(p,rec)||rec.slot==0||rec.slot<=prev||!store.put(rec)) return false;
                prev=rec.slot;++file_records_;++r.records;
                return true;
            });
            persisted_slot_=prev;
            if(!clean) {r.record_tail_cut=true;rec_dirty_=true;}
        } else {
            rec_dirty_=true;            // missing/foreign file: lay down a fresh one on flush
        }
        // ---- events ----
        std::vector<HistoryEvent> list;
        const bool new_events=evt_.exists()&&header_ok(evt_,kEvtMagic,kEvtDisk);
        const bool old_events=!new_events&&evt_.exists()&&
            header_ok(evt_,kEvtMagic,kLegacyEvtDisk);
        if(new_events||old_events) {
            r.event_header_ok=true;
            const size_t disk=old_events?kLegacyEvtDisk:kEvtDisk;
            const bool clean=scan(evt_,disk,[&](const uint8_t* bytes)->bool {
                HistoryEvent e{};
                if(old_events) {
                    uint16_t crc=0;
                    std::memcpy(&crc,bytes+sizeof(LegacyHistoryEvent),sizeof(crc));
                    if(crc!=history_crc16(bytes,sizeof(LegacyHistoryEvent)))return false;
                    LegacyHistoryEvent prior{};
                    std::memcpy(&prior,bytes,sizeof(prior));
                    e.utc=prior.utc;e.kind=prior.kind;e.source=prior.source;
                    e.value=prior.value;e.arg=0;
                } else if(!decode(bytes,e)) return false;
                list.push_back(e);
                return true;
            });
            if(!clean) r.event_tail_cut=true;
        }
        events.restore(list.data(),list.size());
        r.events=events.count();
        evt_synced_total_=events.total();
        // Rewrite migrated old events atomically on next flush, preserving
        // timestamps and prior command/outage records without discarding logs.
        evt_dirty_=old_events||!r.event_header_ok||r.event_tail_cut;
        return r;
    }

    // Persists what is new. Returns false only when something could not be
    // written (state is kept so the next call retries/repairs).
    bool flush(const HistoryStore& store,const EventLog& events) {
        using namespace persist_detail;
        bool ok=true;
        // ---- records ----
        const uint32_t newest=store.newest_slot();
        const bool compact=rec_dirty_||file_records_>=store.capacity()+kHistoryCompactSlack;
        if(!store.empty()&&(compact||newest>persisted_slot_)) {
            if(compact) {
                size_t n=0;
                const uint32_t first=newest>=store.capacity()?newest-static_cast<uint32_t>(store.capacity())+1:1;
                bool good=rec_.replace_begin();
                Chunk c;
                if(good) {
                    const Header h=header(kRecMagic,kRecDisk);
                    good=c.add(&h,sizeof(h),rec_);
                }
                for(uint32_t s=first;good&&s<=newest;++s) {
                    const HistoryRecord* r=store.get(s);
                    if(!r) continue;
                    uint8_t one[kRecDisk];
                    encode(*r,one);
                    good=c.add(one,kRecDisk,rec_);
                    ++n;
                }
                good=good&&c.flush(rec_)&&rec_.replace_commit();
                if(good) {file_records_=n;persisted_slot_=newest;rec_dirty_=false;}
                else {rec_.replace_abort();ok=false;}
            } else {
                size_t n=0;
                bool good=true;
                uint8_t buf[kChunk];
                size_t used=0;
                for(uint32_t s=persisted_slot_+1;good&&s<=newest;++s) {
                    const HistoryRecord* r=store.get(s);
                    if(!r) continue;
                    if(used+kRecDisk>sizeof(buf)) {good=rec_.append(buf,used);used=0;}
                    encode(*r,buf+used);used+=kRecDisk;++n;
                }
                if(good&&used) good=rec_.append(buf,used);
                if(good) {file_records_+=n;persisted_slot_=newest;}
                else {rec_dirty_=true;ok=false;}   // possible torn append: repair by compaction next time
            }
        }
        // ---- events: small, rewritten whole only when something changed ----
        if(evt_dirty_||events.total()!=evt_synced_total_) {
            bool good=evt_.replace_begin();
            Chunk c;
            if(good) {const Header h=header(kEvtMagic,kEvtDisk);good=c.add(&h,sizeof(h),evt_);}
            for(size_t i=events.count();good&&i>0;--i) {             // oldest first
                uint8_t one[kEvtDisk];
                encode(events.newest(i-1),one);
                good=c.add(one,kEvtDisk,evt_);
            }
            good=good&&c.flush(evt_)&&evt_.replace_commit();
            if(good) {evt_synced_total_=events.total();evt_dirty_=false;}
            else {evt_.replace_abort();ok=false;}
        }
        return ok;
    }
    size_t file_records() const {return file_records_;}
    bool needs_repair() const {return rec_dirty_;}
private:
    FileBackend& rec_;
    FileBackend& evt_;
    size_t file_records_=0;
    uint32_t persisted_slot_=0;
    bool rec_dirty_=false,evt_dirty_=false;
    uint64_t evt_synced_total_=0,evt_known_total_=0;

    static constexpr size_t kChunk=968;               // 44 records; small stack buffers only
    struct Chunk {
        uint8_t buf[kChunk];
        size_t used=0;
        bool add(const void* d,size_t n,FileBackend& f) {
            if(used+n>sizeof(buf)) {if(!flush(f)) return false;}
            std::memcpy(buf+used,d,n);used+=n;
            return true;
        }
        bool flush(FileBackend& f) {
            if(!used) return true;
            const bool ok=f.replace_write(buf,used);
            used=0;
            return ok;
        }
    };
    static bool header_ok(FileBackend& f,uint32_t magic,size_t rec_disk) {
        using namespace persist_detail;
        if(f.size()<sizeof(Header)) return false;
        Header h;
        if(!f.read(0,&h,sizeof(h))) return false;
        return h.magic==magic&&h.version==kVersion&&h.record_size==rec_disk;
    }
    // Walks the records after the header in chunks. visit returns false to stop
    // at a bad record. Returns true only when the file ends exactly on a record
    // boundary with every record accepted.
    template<class Visit>
    static bool scan(FileBackend& f,size_t rec_disk,Visit visit) {
        using namespace persist_detail;
        const size_t total=f.size();
        size_t off=sizeof(Header);
        uint8_t buf[kChunk];
        while(off+rec_disk<=total) {
            size_t want=std::min(sizeof(buf)/rec_disk*rec_disk,((total-off)/rec_disk)*rec_disk);
            if(!f.read(off,buf,want)) return false;
            for(size_t i=0;i<want;i+=rec_disk) if(!visit(buf+i)) return false;
            off+=want;
        }
        return off==total;                                   // trailing partial bytes = torn tail
    }
};
} // namespace enevina::cottage
