#pragma once
#include <HardwareSerial.h>
#include <atomic>

namespace enevina::cottage {
enum class PumpUartOwner : uint8_t { None, MitsubishiCn105, DaikinS21 };

// A single worker owns UART1. Routes and control policy read the session epoch
// so changing the serial protocol invalidates outstanding work immediately.
class PumpUart {
public:
    void begin(PumpUartOwner owner,uint32_t baud,uint32_t format);
    void end(PumpUartOwner owner);
    bool owned_by(PumpUartOwner owner) const {return owner_.load()==owner;}
    uint32_t generation() const {return generation_.load();}
    HardwareSerial& port() {return port_;}
    bool drain();
    bool send(const uint8_t* bytes,size_t size);
private:
    HardwareSerial port_{1};
    std::atomic<PumpUartOwner> owner_{PumpUartOwner::None};
    std::atomic<uint32_t> generation_{0};
};
extern PumpUart cottage_pump_uart;
}
