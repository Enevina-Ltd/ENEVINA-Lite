#ifndef COTTAGE_BENCH_MODE
#include "cottage_pump_uart.hpp"

namespace enevina::cottage {
PumpUart cottage_pump_uart;

void PumpUart::begin(PumpUartOwner owner,uint32_t baud,uint32_t format) {
    ++generation_;
    if(owner_.exchange(PumpUartOwner::None)!=PumpUartOwner::None)port_.end();
    constexpr int rx=18,tx=17;
    port_.begin(baud,format,rx,tx);
    owner_.store(owner);
}
void PumpUart::end(PumpUartOwner owner) {
    if(!owned_by(owner))return;
    ++generation_;
    owner_.store(PumpUartOwner::None);
    port_.end();
}
bool PumpUart::drain() {
    for(unsigned i=0;i<128 && port_.available();++i)port_.read();
    return port_.available()==0;
}
bool PumpUart::send(const uint8_t* bytes,size_t size) {
    if(port_.write(bytes,size)!=size)return false;
    port_.flush(true);
    return true;
}
}
#endif
