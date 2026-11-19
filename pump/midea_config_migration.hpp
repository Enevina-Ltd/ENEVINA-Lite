#pragma once
// Pure decision policy for old V3 firmware ↔ new V2/V3 NVS data.
// The V3 firmware cannot write config_v2, but it can modify its own
// config_v1 after a rollback. Never override a deliberately selected V2.
#include <cstdint>
#include <cstring>

namespace enevina::midea {
struct V3SettingsView {
    const char* host;
    const char* device_id;
    const char* token_hex;
    const char* key_hex;
};
inline bool rollback_v3_settings_changed(
    uint8_t selected_protocol,bool legacy_valid,
    const V3SettingsView& current,const V3SettingsView& legacy) {
    if(selected_protocol!=3 || !legacy_valid ||
       !current.host || !current.device_id ||
       !current.token_hex || !current.key_hex ||
       !legacy.host || !legacy.device_id ||
       !legacy.token_hex || !legacy.key_hex)return false;
    return std::strcmp(current.host,legacy.host)!=0 ||
           std::strcmp(current.device_id,legacy.device_id)!=0 ||
           std::strcmp(current.token_hex,legacy.token_hex)!=0 ||
           std::strcmp(current.key_hex,legacy.key_hex)!=0;
}
} // namespace enevina::midea