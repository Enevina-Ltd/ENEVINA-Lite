#pragma once
#include <cstdint>
// A signed and confirmed OTA version is a PENDING history event until the
// event file has been durably flushed and the explicit NVS acknowledgement
// has been stored. The rotating event ring is not a durable dedup index.
namespace enevina::cottage {
// Signed OTA history, independent of the 128-entry rotating event ring.
struct OtaHistoryLast {
    uint32_t magic=0x4F544831u; // OTH1
    uint32_t version=0;
    uint32_t utc=0;
    uint32_t seal=0;
};
static_assert(sizeof(OtaHistoryLast)==16,"fixed OTA summary ABI");
inline uint32_t ota_history_seal(const OtaHistoryLast& s) {
    return s.magic ^ s.version ^ s.utc ^ 0x6D47A2D3u;
}
inline OtaHistoryLast make_ota_history_last(uint32_t version,uint32_t utc) {
    OtaHistoryLast o{};o.version=version;o.utc=utc;o.seal=ota_history_seal(o);
    return o;
}
inline bool valid_ota_history_last(const OtaHistoryLast& s) {
    return s.magic==0x4F544831u && s.version>0 && s.utc>=1500000000 &&
           s.seal==ota_history_seal(s);
}
inline uint32_t ota_pending_history_upgrade(uint32_t confirmed_good,
                                             uint32_t signed_upgrade,
                                             uint32_t history_ack) {
    return confirmed_good>0 && confirmed_good==signed_upgrade &&
           history_ack!=signed_upgrade ? signed_upgrade : 0;
}
}
