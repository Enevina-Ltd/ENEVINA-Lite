#pragma once
// Persistent local user intent for one pump. No new user login or password.
// On reboot, restore the previously enabled state only for the same exact
// configured CN105 endpoint and adapter. Runtime input/freshness gates remain
// authoritative for every physical command.
#include <cstdint>
#include <cstring>
#include "cottage/site_settings.hpp"

namespace enevina::cottage {
struct StoredControlIntent {
    uint32_t magic=0x45434931; // ECI1
    uint16_t version=2;
    uint16_t size=sizeof(StoredControlIntent);
    uint8_t enabled=0;
    char endpoint[128]{};
    char adapter[40]{};
    uint8_t image_sha256[32]{};
};

inline StoredControlIntent control_intent_for(
        const SiteSettings& settings, bool enabled,
        const uint8_t firmware_image_sha256[32]) {
    StoredControlIntent out{};
    out.enabled=enabled ? 1 : 0;
    std::memcpy(out.endpoint,settings.cn105_url,sizeof(out.endpoint));
    std::memcpy(out.adapter,settings.pump_adapter,sizeof(out.adapter));
    if(firmware_image_sha256)
        std::memcpy(out.image_sha256,firmware_image_sha256,
                    sizeof(out.image_sha256));
    return out;
}
inline bool control_intent_restorable(
        const StoredControlIntent& saved,const SiteSettings& now,
        const uint8_t running_image_sha256[32]) {
    return saved.magic==StoredControlIntent{}.magic &&
        saved.version==2 &&
        saved.size==sizeof(StoredControlIntent) &&
        running_image_sha256!=nullptr &&
        saved.enabled==1 &&
        std::memchr(saved.endpoint,0,sizeof(saved.endpoint))!=nullptr &&
        std::memchr(saved.adapter,0,sizeof(saved.adapter))!=nullptr &&
        std::strcmp(saved.endpoint,now.cn105_url)==0 &&
        std::strcmp(saved.adapter,now.pump_adapter)==0 &&
        std::memcmp(saved.image_sha256,running_image_sha256,32)==0;
}
} // namespace enevina::cottage
