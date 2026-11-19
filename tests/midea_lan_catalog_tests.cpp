#include "pump/adapter_catalog.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    using namespace enevina;
    const auto* m=find_adapter("midea_lan");
    assert(m);
    assert(m->link==AdapterLink::VendorLanProtocol);
    assert(m->maturity==AdapterMaturity::FirmwareControlExperimental);
    assert(std::strstr(m->commands,"manual 0x40")!=nullptr);
    assert(m->command_readback);
    assert(m->setpoint_min_c==0 && m->setpoint_max_c==0);
    assert(adapter_selectable_in_lite_firmware("midea_lan"));
    assert(!adapter_allowed_cn105_physical_writes("midea_lan"));
    assert(adapter_allowed_cn105_physical_writes("mitsubishi_cn105_esphome"));
    assert(!adapter_allowed_cn105_physical_writes(""));
    assert(!adapter_allowed_cn105_physical_writes(nullptr));
    assert(!adapter_selectable_in_lite_firmware("nibe_s_modbus_tcp"));
    assert(!adapter_selectable_in_lite_firmware("gree_ewpe_lan"));
    assert(!adapter_selectable_in_lite_firmware("unknown"));
    std::cout<<"Midea experimental manual control and CN105 isolation PASS\n";
}