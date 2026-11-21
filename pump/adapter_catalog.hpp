#pragma once
// Manufacturer-independent heat-pump adapter catalog.
//
// Each entry states what is ACTUALLY implemented and verified in THIS
// repository, not what a manufacturer or a third-party project advertises.
// Research-only rows stay unselectable. Midea V2/V3 AC has a tested Frezzer
// setpoint pilot; other models retain protocol-level experimental support.
// Selecting an adapter never authorizes writes: the Supervisor / armed
// control gate and a model-specific approved test plan stay mandatory.
#include "pump/pump_adapter.hpp"
#include <cstddef>
#include <cstring>

namespace enevina {
enum class AdapterMaturity : uint8_t {
    // Implemented, host-tested and used read-only / armed-pilot on a real pump.
    FieldPilot,
    // Implemented and host-tested, not wired into this ESP32-S3 firmware.
    HostTestedOnly,
    // Native ESP32 firmware read-only path and setup; no model-level field proof.
    FirmwareReadOnly,
    // Native manual command path and readback; field coverage varies by model.
    FirmwareControlExperimental,
    // Protocol researched/documented only (Enevina level 0). Not selectable.
    DocumentedOnly
};
enum class AdapterLink : uint8_t {
    EsphomeHttpGateway, // separate local ESPHome bridge (e.g. CN105 UART)
    HttpJson,           // local HTTP JSON gateway (e.g. HeishaMon)
    ModbusTcp,
    VendorLanProtocol,  // manufacturer Wi-Fi module local LAN protocol
    SerialBus           // RS485 / eBUS / CAN: needs a physical bus interface
};
struct AdapterCatalogEntry {
    const char* id;            // stable settings key
    const char* manufacturer;
    const char* models;        // exact models that have evidence, not a family claim
    HeatPumpType type;
    AdapterLink link;
    AdapterMaturity maturity;
    const char* measurements;  // what the adapter really reads
    const char* commands;      // what it can send (always via Supervisor)
    double setpoint_min_c;     // device setpoint limits, 0/0 = none implemented
    double setpoint_max_c;
    double setpoint_step_c;
    const char* freshness;     // how sample freshness is established
    bool command_readback;     // command result read back from the device
    const char* evidence;      // where the evidence lives
};

inline constexpr AdapterCatalogEntry adapter_catalog[]={
    {"mitsubishi_cn105_esphome","Mitsubishi Electric","MSZ-FD25VA / MUZ-FD25VA",
     HeatPumpType::AirToAir,AdapterLink::EsphomeHttpGateway,AdapterMaturity::FieldPilot,
     "room temp (pump), mode, setpoint, compressor Hz via ESPHome /events SSE",
     "HEAT setpoint 16-26 C (0.5 C), HEAT, OFF via ESPHome POST",
     16.0,26.0,0.5,
     "SSE receipt != UART read; strict source proof gate separate (docs/field)",
     true,"PR #24 readback, PR #28 HTTP 411 field fix, docs/field/CN105_SOURCE_PROOF_20261117.md"},
    {"panasonic_heishamon_http","Panasonic Aquarea","HeishaMon-compatible (model unverified)",
     HeatPumpType::AirToWater,AdapterLink::HttpJson,AdapterMaturity::HostTestedOnly,
     "HeishaMon /json temperatures, mode (pump/heishamon_adapter.cpp)",
     "none implemented",0,0,0,"HTTP receipt only, no source timestamp",false,
     "pump/heishamon_adapter.cpp, tests/core_tests.cpp (W5500 G0 firmware only)"},
    {"nibe_s_modbus_tcp","NIBE","S1155/S1255/S1156/S1256 (research)",
     HeatPumpType::GroundSource,AdapterLink::ModbusTcp,AdapterMaturity::DocumentedOnly,
     "not implemented","not implemented",0,0,0,"not implemented",false,
     "PR #25 GSHP_ASHP_ILP_LOCAL_PROTOCOLS.md"},
    {"thermia_genesis_modbus_tcp","Thermia","Genesis controller (research)",
     HeatPumpType::GroundSource,AdapterLink::ModbusTcp,AdapterMaturity::DocumentedOnly,
     "not implemented","not implemented",0,0,0,"not implemented",false,
     "PR #25 GSHP_ASHP_ILP_LOCAL_PROTOCOLS.md"},
    {"luxtronik2_tcp","Alpha Innotec / Novelan","Luxtronik 2.0 (research)",
     HeatPumpType::GroundSource,AdapterLink::VendorLanProtocol,AdapterMaturity::DocumentedOnly,
     "not implemented","not implemented",0,0,0,"not implemented",false,
     "PR #25 GSHP_ASHP_ILP_LOCAL_PROTOCOLS.md"},
    {"midea_lan","Midea / OEM","Frezzer Nordic (V3 pilot); Midea/OEM AC 0xAC (V2/V3)",
     HeatPumpType::AirToAir,AdapterLink::VendorLanProtocol,AdapterMaturity::FirmwareControlExperimental,
     "V2/V3 AC state, model/capabilities and optional temperatures",
     "manual 0x40 / BB / B0 with readback; automatic thermostat/price via the same queue",
     0,0,0,"V3 authenticated, V2 fixed shared key; no pump-origin timestamp",true,
     "docs/integrations/MIDEA_MODEL_CATALOG.md; docs/field/MIDEA_LAN_CONTROL_20261121.md"},
    {"gree_ewpe_lan","Gree / EWPE Smart OEM","Local V1/V2 (no field-verified models)",
     HeatPumpType::AirToAir,AdapterLink::VendorLanProtocol,AdapterMaturity::FirmwareReadOnly,
     "direct LAN discovery, pairing, operating state, Celsius setpoint and optional temperature",
     "not enabled",0,0,0,"direct LAN response; reported values have no pump timestamp",false,
     "docs/integrations/GREE_LAN.md"},
    {"daikin_local_api","Daikin","BRP069Axx/Bxx, BRP072A42/A43 (research)",
     HeatPumpType::AirToAir,AdapterLink::VendorLanProtocol,AdapterMaturity::DocumentedOnly,
     "not implemented","not implemented",0,0,0,"not implemented",false,
     "PR #25 LOCAL_WIFI_COMPATIBILITY.md"},
};
inline constexpr size_t adapter_catalog_size=sizeof(adapter_catalog)/sizeof(adapter_catalog[0]);

inline const AdapterCatalogEntry* find_adapter(const char* id) {
    if(!id) return nullptr;
    for(const auto& e:adapter_catalog) if(std::strcmp(e.id,id)==0) return &e;
    return nullptr;
}
// The only adapter identity authorised to use the CN105 physical writer.
// Selection of the Midea adapter must NEVER grant CN105 writes.
inline bool adapter_allowed_cn105_physical_writes(const char* id) {
    return id && std::strcmp(id,"mitsubishi_cn105_esphome")==0;
}
// Only adapters implemented in THIS ESP32-S3 cottage firmware may be selected.
inline bool adapter_selectable_in_lite_firmware(const char* id) {
    const auto* e=find_adapter(id);
    // Midea V2/V3 writes use the local LAN worker.
    return e && (e->maturity==AdapterMaturity::FieldPilot ||
        e->maturity==AdapterMaturity::FirmwareReadOnly ||
        (std::strcmp(e->id,"midea_lan")==0 &&
         e->maturity==AdapterMaturity::FirmwareControlExperimental));
}
inline const char* adapter_maturity_name(AdapterMaturity m) {
    switch(m) {
    case AdapterMaturity::FieldPilot:return "FIELD_PILOT";
    case AdapterMaturity::HostTestedOnly:return "HOST_TESTED_ONLY";
    case AdapterMaturity::FirmwareReadOnly:return "FIRMWARE_READ_ONLY";
    case AdapterMaturity::FirmwareControlExperimental:return "FIRMWARE_CONTROL_EXPERIMENTAL";
    case AdapterMaturity::DocumentedOnly:return "DOCUMENTED_ONLY";
    }
    return "UNKNOWN";
}
inline const char* adapter_link_name(AdapterLink l) {
    switch(l) {
    case AdapterLink::EsphomeHttpGateway:return "ESPHOME_HTTP_GATEWAY";
    case AdapterLink::HttpJson:return "HTTP_JSON";
    case AdapterLink::ModbusTcp:return "MODBUS_TCP";
    case AdapterLink::VendorLanProtocol:return "VENDOR_LAN";
    case AdapterLink::SerialBus:return "SERIAL_BUS";
    }
    return "UNKNOWN";
}
} // namespace enevina
