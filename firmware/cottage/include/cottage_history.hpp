#pragma once
// 30-day history (15-minute records) and event log: PSRAM ring, LittleFS
// persistence on the otherwise unused "spiffs" partition, hourly batched
// flushes, GET /api/history. See cottage/history_store.hpp for the rules.
#include "cottage/history_store.hpp"
class WebServer;

namespace enevina_firmware {
// Once in setup(): allocates the ring in PSRAM, mounts the history partition
// (formats ONLY that partition if it has never been formatted) and loads what
// was saved. If anything fails the history keeps working in RAM only and the
// API says so; no other storage (NVS, OTA, settings) is touched.
void history_initialize();
// Every ~15 s from one worker task.
void history_observe(const enevina::cottage::HistoryInputs& inputs);
void history_http(WebServer& web);
}
