#pragma once
// 30-day history (15-minute records) and event log: PSRAM ring, LittleFS
// persistence on the otherwise unused "spiffs" partition, quarter-hour
// flushes, GET /api/history. See cottage/history_store.hpp for the rules.
#include "cottage/history_store.hpp"
#include "cottage/model_checkpoint.hpp"
class CottageWebServer;

namespace enevina_firmware {
// Once in setup(): allocates the ring in PSRAM, mounts the history partition
// (formats ONLY that partition if it has never been formatted) and loads what
// was saved. If anything fails the history keeps working in RAM only and the
// API says so; no other storage (NVS, OTA, settings) is touched.
void history_initialize();
// Flush and freeze the recorder only for final activation.
bool history_flush_for_ota();
void history_resume_after_ota();
// Every ~15 s from one worker task.
void history_observe(const enevina::cottage::HistoryInputs& inputs);
void history_http(CottageWebServer& web);
// Newest event of a kind (persisted across reboots): utc and its 32-bit arg.
// Nonblocking optional lookup: false also means the history store is busy.
bool history_last_event(uint8_t kind,uint32_t& utc,uint32_t& arg);
uint32_t history_read_busy_count();
// Separate fixed/variable model files on the already mounted LittleFS.
// A failed mount/lock/read is not evidence that a checkpoint is absent.
enevina::cottage::CheckpointRead history_model_read(bool variable,uint8_t* data,size_t cap,size_t& used);
bool history_model_write(bool variable,const uint8_t* data,size_t used);
bool history_model_erase(bool variable);
bool history_model_present(bool variable); // true also when absence cannot be verified
// Completed maintenance observations, same atomic file/lock as model storage.
// The fixed-model reset also erases this file or advances its bound epoch.
enevina::cottage::CheckpointRead history_maintenance_read(uint8_t* data,size_t cap,size_t& used);
bool history_maintenance_write(const uint8_t* data,size_t used);
enevina::cottage::CheckpointRead history_baseload_read(uint8_t* data,size_t cap,size_t& used);
bool history_baseload_write(const uint8_t* data,size_t used);
bool history_maintenance_present(); // true if absence cannot be verified
}
