#include <Arduino.h>
#include <cstring>
#include "cottage_config.hpp"
#include "cottage_site_settings.hpp"
// FMI location comes from the browser settings (compile-time value is only
// the default). The weather reader reads it once when it starts at boot.
static char cottage_fmi_latlon_boot[32];
static const char* cottage_fmi_latlon() {
    const auto s=site_settings_snapshot();
    strlcpy(cottage_fmi_latlon_boot,s.fmi_latlon,sizeof(cottage_fmi_latlon_boot));
    return cottage_fmi_latlon_boot;
}
#ifndef CONFIG_ENEVINA_FMI_LATLON
#define CONFIG_ENEVINA_FMI_LATLON cottage_fmi_latlon()
#endif
#include "../../main/weather.cpp"
