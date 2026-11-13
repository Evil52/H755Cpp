#pragma once
#include <cstdint>
namespace h755 {
class Timebase {
public:
    Timebase() = default;
    ~Timebase();
    Timebase(const Timebase&) = delete;
    Timebase& operator=(const Timebase&) = delete;
    bool start(std::uint32_t hz) noexcept;
    std::uint32_t now() const noexcept { return ticks_; }
    static void interrupt() noexcept;
private:
    volatile std::uint32_t ticks_{};
    static Timebase* volatile owner_;
};
}
extern "C" void SysTick_Handler();
