#include "pump/midea_config_migration.hpp"
#include <cassert>
#include <iostream>
using enevina::midea::V3SettingsView;
using enevina::midea::rollback_v3_settings_changed;

int main() {
    // Normal V3 upgrade: the new firmware mirrors its current V3 settings
    // to both NVS keys. Re-reading an identical v1 must keep v2.
    const V3SettingsView v3{"192.168.1.10","123456","aaa","bbb"};
    assert(!rollback_v3_settings_changed(3,true,v3,v3));
    // After rollback, the older V3 binary updates ONLY config_v1:
    // the re-upgraded binary must recover the latest device details.
    const V3SettingsView changed_ip{"192.168.1.11","123456","aaa","bbb"};
    const V3SettingsView changed_cred{"192.168.1.10","123456","ccc","ddd"};
    const V3SettingsView changed_id{"192.168.1.10","999999","aaa","bbb"};
    assert(rollback_v3_settings_changed(3,true,v3,changed_ip));
    assert(rollback_v3_settings_changed(3,true,v3,changed_cred));
    assert(rollback_v3_settings_changed(3,true,v3,changed_id));
    // Deliberate V2 choice must remain V2 even if a legacy v1 record
    // exists and differs. Likewise an absent/invalid old key is ignored.
    assert(!rollback_v3_settings_changed(2,true,v3,changed_ip));
    assert(!rollback_v3_settings_changed(2,true,v3,changed_cred));
    assert(!rollback_v3_settings_changed(3,false,v3,changed_ip));
    const V3SettingsView missing{"192.168.1.1",nullptr,"a","b"};
    assert(!rollback_v3_settings_changed(3,true,v3,missing));
    std::cout<<"Midea V3 rollback-change detection / V2 selection safety PASS\n";
}