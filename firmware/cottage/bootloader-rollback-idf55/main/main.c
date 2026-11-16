#include "esp_log.h"
#include "esp_ota_ops.h"
void app_main(void) {
    // Only used to build a proper ESP-IDF rollback-enabled bootloader.
    // This test application is NEVER installed over the Arduino application.
    ESP_LOGI("enevina_c24","standalone rollback bootloader build verification");
}
