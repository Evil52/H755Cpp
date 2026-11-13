#pragma once
#include <cstdint>
namespace h755::ethernet { class CmsisMdioRegisters; }
namespace h755 {
class Board {
public:
    Board() = default;
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    void configure() noexcept;
    ethernet::CmsisMdioRegisters mdio_registers() noexcept;
    std::uint32_t hclk_hz() const noexcept;
    std::uint32_t m4_marker() const noexcept;
    void green(bool enabled) noexcept;
    void red(bool enabled) noexcept;
    void idle() noexcept;
};
namespace board { void emergency_led() noexcept; }
}
