#pragma once
#include <cstdint>
// A signed and confirmed OTA version is a PENDING history event until the
// event file has been durably flushed and the explicit NVS acknowledgement
// has been stored. The rotating event ring is not a durable dedup index.
namespace enevina::cottage {
inline uint32_t ota_pending_history_upgrade(uint32_t confirmed_good,
                                             uint32_t signed_upgrade,
                                             uint32_t history_ack) {
    return confirmed_good>0 && confirmed_good==signed_upgrade &&
           history_ack!=signed_upgrade ? signed_upgrade : 0;
}
}
