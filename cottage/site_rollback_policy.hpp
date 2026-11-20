#pragma once
// A site NVS migration may run during c23_initialize(), BEFORE the OTA
// transaction service is initialized. A default-false 'pending' flag alone
// is NOT proof that the running firmware was confirmed by ESP-IDF.
namespace enevina::cottage {
inline bool site_old_keys_reclaimable(bool running_image_valid,
                                      bool ota_confirmation_pending) {
    return running_image_valid && !ota_confirmation_pending;
}
}
