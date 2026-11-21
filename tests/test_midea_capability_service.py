"""Execute the service's real command admission policy with the native codec.

The extracted block has no Arduino/network dependencies. This catches an API
admission regression that protocol tests alone miss: routing new fields through
the legacy five-field validator rejects every optional command before queuing.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MideaCapabilityService(unittest.TestCase):
    def test_optional_fields_reach_native_command_validation(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ is required for the native service policy test")
        service = (ROOT / "firmware/cottage/src/cottage_midea_service.cpp").read_text()
        # Compile the production policy itself, as the dashboard VM tests do
        # for the JavaScript; no duplicate implementation or hardware mocks.
        policy = service.split("struct BooleanControl {", 1)[1].split(
            "void add_profile_status(", 1
        )[0]
        source = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "pump/midea_ac_protocol.hpp"
''' + "struct BooleanControl {" + policy + r'''
using namespace enevina::midea;
void report(AcCapabilities& caps,AcFeature feature,bool supported) {
    const uint64_t bit=uint64_t{1}<<static_cast<uint8_t>(feature);
    caps.known|=bit;
    if(supported)caps.supported|=bit;else caps.supported&=~bit;
    caps.received=true;
}
int main() {
    State state{};state.power=true;state.mode=4;state.setpoint_c=22;
    state.fan=40;state.swing=0;state.freeze_protection_known=true;
    state.fahrenheit_known=state.follow_me_known=true;
    state.independent_aux_heat_known=state.target_humidity_known=true;
    state.comfort_mode_known=true;
    state.properties_known=ControlEco|ControlSleep|ControlAnion|ControlFreezeProtection|ControlDisplay;
    auto& caps=state.profile.capabilities;
    report(caps,AcFeature::Eco,true);report(caps,AcFeature::Display,true);
    report(caps,AcFeature::VerticalAngle,true);report(caps,AcFeature::RateSelect,true);
    report(caps,AcFeature::FreezeProtection,true);

    // B5 unsupported can make full-state encoding safe without supplying a
    // live frost bit. The API must not publish that inference as observed off.
    State inferred=state;
    report(inferred.profile.capabilities,AcFeature::FreezeProtection,false);
    inferred.freeze_protection_known=true;inferred.freeze_protection_inferred=true;
    inferred.freeze_protection=false;inferred.properties_known|=ControlFreezeProtection;
    assert(!observed_control_field(inferred,ControlFreezeProtection));
    ControlPatch inferred_basic{};inferred_basic.changed=ControlTemperature;inferred_basic.setpoint_c=23;
    assert(control_request_error(inferred,inferred_basic)==nullptr);
    assert(observed_control_field(state,ControlFreezeProtection));

    // An A0-only sample does not provide the classic preserved fields. A
    // suggested frost choice must not imply those other missing bits are safe.
    State incomplete=state;
    incomplete.fahrenheit_known=incomplete.follow_me_known=false;
    incomplete.independent_aux_heat_known=incomplete.target_humidity_known=false;
    incomplete.freeze_protection_known=false;
    assert(!std::strcmp(control_request_error(incomplete,inferred_basic),"UNSUPPORTED_STATE"));
    State ambiguous=state;ambiguous.classic_aux_bit_ambiguous=true;
    assert(!std::strcmp(control_request_error(ambiguous,inferred_basic),"UNSUPPORTED_STATE"));
    State missing_comfort=state;missing_comfort.comfort_mode_known=false;
    assert(!std::strcmp(control_request_error(missing_comfort,inferred_basic),"UNSUPPORTED_STATE"));
    ControlPatch property{};property.changed=ControlDisplay;property.display=true;
    assert(control_request_error(incomplete,property)==nullptr);
    assert(control_request_error(ambiguous,property)==nullptr);
    assert(control_request_error(missing_comfort,property)==nullptr);

    // A supported destination mode remains offered even when its range cannot
    // inherit the current target. The real mode-only command must still fail.
    State changing=state;changing.setpoint_c=30;
    changing.profile.capabilities.temperatures[2]={true,18,26};
    ControlPatch mode_only{};mode_only.changed=ControlMode;mode_only.mode=2;
    assert(!valid_ac_control_patch(changing,mode_only));
    assert(mode_control_available(changing,2));
    ControlPatch combined=mode_only;combined.changed|=ControlTemperature;combined.setpoint_c=26;
    assert(control_request_error(changing,combined)==nullptr);
    assert(changing.setpoint_c==30 && changing.mode==4);
    changing.setpoint_c=22.5;
    assert(!valid_ac_control_patch(changing,mode_only));
    assert(mode_control_available(changing,2));
    report(changing.profile.capabilities,AcFeature::CoolMode,false);
    assert(!mode_control_available(changing,2));

    // B5 fan enumeration 3 permits Low/High, not Auto. Leaving Dry remains
    // possible with an explicit supported fan, so availability must offer it.
    State drying=state;drying.mode=3;drying.fan=40;
    auto& dry_caps=drying.profile.capabilities;
    report(dry_caps,AcFeature::FanSilent,false);report(dry_caps,AcFeature::FanLow,true);
    report(dry_caps,AcFeature::FanMedium,false);report(dry_caps,AcFeature::FanHigh,true);
    report(dry_caps,AcFeature::FanAuto,false);report(dry_caps,AcFeature::FanCustom,false);
    ControlPatch dry_exit{};dry_exit.changed=ControlMode|ControlTemperature;
    dry_exit.mode=4;dry_exit.setpoint_c=22;
    assert(!valid_ac_control_patch(drying,dry_exit));
    assert(mode_control_available(drying,4));
    dry_exit.changed|=ControlFan;dry_exit.fan=40;
    assert(control_request_error(drying,dry_exit)==nullptr);
    dry_exit.fan=102;assert(!valid_ac_control_patch(drying,dry_exit));
    report(dry_caps,AcFeature::FanLow,false);report(dry_caps,AcFeature::FanHigh,false);
    assert(!mode_control_available(drying,4));

    // The same service table parses HTTP boolean names and assigns patch bits.
    for(const char* name:{"eco","display"}) {
        ControlPatch patch{};
        for(const auto& field:boolean_controls)if(!std::strcmp(field.name,name)) {
            patch.changed=field.field;patch.*(field.patch)=true;
        }
        assert(control_patch_fields_valid(patch));
        assert(control_request_error(state,patch)==nullptr);
    }
    ControlPatch numeric{};numeric.changed=ControlVerticalAngle;numeric.vertical_angle=25;
    assert(control_patch_fields_valid(numeric));
    assert(control_request_error(state,numeric)==nullptr);
    numeric.vertical_angle=37;
    assert(!std::strcmp(control_request_error(state,numeric),"UNSUPPORTED_CONTROL"));

    ControlPatch mixed{};mixed.changed=ControlTemperature|ControlDisplay;
    mixed.setpoint_c=23;mixed.display=true;
    assert(!std::strcmp(control_request_error(state,mixed),"MIXED_COMMAND_FAMILIES"));
    ControlPatch empty{};assert(!control_patch_fields_valid(empty));
    empty.changed=uint32_t{1}<<31;assert(!control_patch_fields_valid(empty));

    state.freeze_protection_known=false;
    state.properties_known&=~ControlFreezeProtection;
    ControlPatch basic{};basic.changed=ControlTemperature;basic.setpoint_c=23;
    assert(!std::strcmp(control_request_error(state,basic),"MISSING_FROST_STATE"));
    ControlPatch selective{};selective.changed=ControlDisplay;selective.display=true;
    assert(control_request_error(state,selective)==nullptr);
    basic.changed|=ControlFreezeProtection;basic.freeze_protection=false;
    assert(control_request_error(state,basic)==nullptr);

    // Capability support and a live setting are independent. Explicit negative
    // support cannot be overridden by an old observation of that field.
    report(caps,AcFeature::Eco,false);
    assert(!optional_control_supported(state,ControlEco,AcFeature::Eco));
    report(caps,AcFeature::FreezeProtection,false);
    basic.changed=ControlTemperature;
    assert(control_request_error(state,basic)==nullptr);
    state.profile.family=AcProtocol::Subprotocol;
    report(caps,AcFeature::FreezeProtection,true);report(caps,AcFeature::Anion,true);
    assert(!optional_control_supported(state,ControlFreezeProtection,AcFeature::FreezeProtection));
    assert(!optional_control_supported(state,ControlAnion,AcFeature::Anion));
    std::cout<<"PASS service optional-field admission, native validation and missing-frost policy\n";
}
'''
        with tempfile.TemporaryDirectory(prefix="midea-service-policy-") as folder:
            path = Path(folder)
            (path / "policy.cpp").write_text(source)
            built = subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                str(path / "policy.cpp"), str(ROOT / "pump/midea_ac_protocol.cpp"),
                str(ROOT / "pump/midea_ac_capabilities.cpp"),
                str(ROOT / "pump/midea_v3_protocol.cpp"), "-lcrypto", "-o", str(path / "policy")
            ], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(path / "policy")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
