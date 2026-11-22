#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_daikin_s21_selected();
bool cottage_daikin_s21_needs_worker();
void cottage_daikin_s21_register_routes(WebServer&);
void cottage_daikin_s21_tick();
uint32_t cottage_daikin_s21_configuration_generation();
uint32_t cottage_daikin_s21_calibration_identity();
enevina::PumpRoomState cottage_daikin_s21_room_state();

uint32_t cottage_daikin_s21_bus_generation();
bool cottage_daikin_s21_automatic_ready();
bool cottage_daikin_s21_automatic_enabled();
void cottage_daikin_s21_automatic_cancel();
bool cottage_daikin_s21_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_daikin_s21_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
