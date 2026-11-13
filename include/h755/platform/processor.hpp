#pragma once
#include <cstdint>
namespace h755::processor {
bool start_tick(std::uint32_t cycles) noexcept;
void stop_tick() noexcept;
void disable_interrupts() noexcept;
void barrier() noexcept;
void idle() noexcept;
}
