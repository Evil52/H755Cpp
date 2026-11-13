#include "h755/timebase.hpp"
#include "h755/platform/processor.hpp"
namespace h755 {
Timebase* volatile Timebase::owner_{};
bool Timebase::start(std::uint32_t hz) noexcept
{
    if (owner_ || hz < 1000U || hz % 1000U != 0U || hz / 1000U > 0x1000000U)
        return false;
    ticks_ = 0U;
    owner_ = this;
    if (!processor::start_tick(hz / 1000U)) {
        owner_ = nullptr;
        return false;
    }
    return true;
}
Timebase::~Timebase()
{
    if (owner_ == this) {
        processor::stop_tick();
        owner_ = nullptr;
    }
}
void Timebase::interrupt() noexcept
{
    if (auto* clock = owner_) clock->ticks_ = clock->ticks_ + 1U;
}
}
extern "C" void SysTick_Handler() { h755::Timebase::interrupt(); }
