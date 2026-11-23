#include "cn105.h"
#include <cinttypes>
#include <cstdio>
#include <ctime>

#ifdef USE_ESP32
#include <esp_timer.h>
#include <nvs.h>
#endif

namespace esphome {

#ifdef USE_ESP32
namespace {
constexpr char epoch_namespace[]="enevina_cn105";
constexpr char epoch_key[]="poll_epoch";

uint32_t reserve_boot_epoch(uint32_t utc) {
    nvs_handle_t handle;
    if(nvs_open(epoch_namespace,NVS_READWRITE,&handle)!=ESP_OK)return 0;
    uint32_t previous=0;
    const auto read=nvs_get_u32(handle,epoch_key,&previous);
    uint32_t verified=0;
    if((read==ESP_OK || read==ESP_ERR_NVS_NOT_FOUND) && previous<UINT32_MAX) {
        const uint32_t epoch=utc>previous?utc:previous+1;
        if(nvs_set_u32(handle,epoch_key,epoch)!=ESP_OK || nvs_commit(handle)!=ESP_OK ||
           nvs_get_u32(handle,epoch_key,&verified)!=ESP_OK || verified!=epoch)
            verified=0;
    }
    nvs_close(handle);
    return verified;
}

uint64_t mono_ms() { return static_cast<uint64_t>(esp_timer_get_time()/1000); }
}
#endif

void CN105Climate::begin_enevina_read_cycle_() {
#ifdef USE_ESP32
    if(!enevina_snapshot_sensor_)return;
    if(enevina_poll_.active())this->invalidate_enevina_snapshot_(true);
    const int64_t utc=std::time(nullptr);
    if(utc<1700000000 || utc>UINT32_MAX) {
        this->invalidate_enevina_snapshot_(true);
        return;
    }
    if(!enevina_epoch_attempted_) {
        enevina_epoch_attempted_=true;
        const auto epoch=reserve_boot_epoch(static_cast<uint32_t>(utc));
        if(!epoch) ESP_LOGW("enevina_cn105","Cannot persist UART proof boot epoch");
        enevina_poll_.set_epoch(epoch);
    }
    if(enevina_poll_.snapshot().epoch)enevina_poll_.begin();
#endif
}

void CN105Climate::observe_enevina_tx_(const uint8_t* packet,int length) {
#ifdef USE_ESP32
    if(!enevina_poll_.active())return;
    if(length!=22 || packet[1]!=0x42 || !this->isHeatpumpConnected()) {
        this->invalidate_enevina_snapshot_(this->isHeatpumpConnected());
        return;
    }
    enevina_poll_.sent(packet[5],mono_ms());
    if(!enevina_poll_.active())this->invalidate_enevina_snapshot_(true);
#else
    (void)packet;
    (void)length;
#endif
}

void CN105Climate::observe_enevina_rx_() {
#ifdef USE_ESP32
    if(!enevina_poll_.active())return;
    if(!this->isHeatpumpConnected()) {
        this->invalidate_enevina_snapshot_(false);
        return;
    }
    if(enevina_poll_.receive(this->parser_.raw(),this->parser_.frame_size(),
                             mono_ms(),std::time(nullptr))) {
        this->publish_enevina_snapshot_(true,true);
    } else if(!enevina_poll_.active()) {
        this->invalidate_enevina_snapshot_(true);
    }
#endif
}

void CN105Climate::invalidate_enevina_snapshot_(bool connected) {
    enevina_poll_.discard();
    if(enevina_snapshot_published_)this->publish_enevina_snapshot_(connected,false);
}

void CN105Climate::publish_enevina_snapshot_(bool connected,bool complete) {
    const auto& s=enevina_poll_.snapshot();
    char json[256];
    const auto n=std::snprintf(json,sizeof(json),
        "{\"schema\":1,\"boot\":%" PRIu32 ",\"seq\":%" PRIu32
        ",\"oldest_utc\":%" PRId64 ",\"latest_utc\":%" PRId64
        ",\"uart_connected\":%s,\"complete_poll\":%s,\"indoor_c\":%.1f,"
        "\"setpoint_c\":%.1f,\"compressor_hz\":%u,\"mode\":\"%s\"}",
        s.epoch,s.sequence,s.oldest_utc,s.latest_utc,connected?"true":"false",
        complete?"true":"false",s.indoor_half_c/2.0,s.target_half_c/2.0,
        s.compressor_hz,s.mode);
    if(n>0 && static_cast<std::size_t>(n)<sizeof(json)) {
        enevina_snapshot_sensor_->publish_state(json);
        enevina_snapshot_published_=complete;
    }
}

} // namespace esphome
