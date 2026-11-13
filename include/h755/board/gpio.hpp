#pragma once
#include <cstdint>
namespace h755::gpio {
inline constexpr std::uint32_t green = 1U;
inline constexpr std::uint32_t red = 1U << 14;

template<unsigned Pin, unsigned Function, class Registers>
void alternate(Registers& port) noexcept
{
    static_assert(Pin < 16U && Function < 16U);
    constexpr auto shift = Pin * 2U;
    port.MODER = (port.MODER & ~(3U << shift)) | (2U << shift);
    port.OTYPER = port.OTYPER & ~(1U << Pin);
    port.OSPEEDR = (port.OSPEEDR & ~(3U << shift)) | (3U << shift);
    port.PUPDR = port.PUPDR & ~(3U << shift);
    constexpr auto bank = Pin / 8U;
    constexpr auto af_shift = (Pin % 8U) * 4U;
    port.AFR[bank] = (port.AFR[bank] & ~(15U << af_shift)) | (Function << af_shift);
}
template<class Registers>
void configure_leds(Registers& port) noexcept
{
    port.BSRR = (green | red) << 16;
    port.MODER = (port.MODER & ~((3U << 28) | 3U)) | (1U << 28) | 1U;
}
template<class Registers>
void set_green(Registers& port, bool enabled) noexcept
{
    port.BSRR = enabled ? green : green << 16;
}
template<class Registers>
void set_red(Registers& port, bool enabled) noexcept
{
    port.BSRR = enabled ? red : red << 16;
}
}
