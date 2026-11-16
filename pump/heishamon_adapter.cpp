#include "pump/heishamon_adapter.hpp"
#include <cmath>

namespace enevina {
bool HeishaMonAdapter::poll(PumpTelemetry& output) {
    double inlet,outlet,outdoor,dhw,compressor,defrost,mode;
    if (!transport_.read_number("Main_Inlet_Temp",inlet) ||
        !transport_.read_number("Main_Outlet_Temp",outlet) ||
        !transport_.read_number("Outside_Temp",outdoor) ||
        !transport_.read_number("DHW_Temp",dhw) ||
        !transport_.read_number("Compressor_Freq",compressor) ||
        !transport_.read_number("Defrosting_State",defrost) ||
        !transport_.read_number("Operating_Mode_State",mode)) return false;
    if (!std::isfinite(inlet) || inlet<=-40 || inlet>=100 ||
        !std::isfinite(outlet) || outlet<=-40 || outlet>=100 ||
        !std::isfinite(outdoor) || outdoor<=-50 || outdoor>=60 ||
        !std::isfinite(dhw) || dhw<=0 || dhw>=100 ||
        !std::isfinite(compressor) || compressor<0 || compressor>=200 ||
        (defrost!=0 && defrost!=1) || mode<0 || mode>8 ||
        std::floor(mode)!=mode) return false;
    output={inlet,outlet,outdoor,dhw,compressor,defrost==1,static_cast<int>(mode)};
    return true;
}
}
