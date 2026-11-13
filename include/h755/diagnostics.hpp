#pragma once
#include "h755/ethernet/lan8742.hpp"
#include <cstdint>

namespace h755 {
struct BringupDiagnostics {
    std::uint32_t magic, state, heartbeat, hclk_hz, phy_address;
    std::uint32_t phy_id1, phy_id2, mdio_errors, fault, m4_marker;
};
struct LinkDiagnostics { std::uint32_t state, samples, errors; };

class Diagnostics {
public:
    Diagnostics(volatile BringupDiagnostics& boot, volatile LinkDiagnostics& link) noexcept
        : boot_{boot}, link_{link} {}
    void begin(std::uint32_t hz) noexcept;
    void parked(std::uint32_t marker) noexcept;
    void scanning() noexcept;
    void found(std::uint32_t address, std::uint16_t id1, std::uint16_t id2) noexcept;
    void finish_scan(bool found) noexcept;
    void mdio_error() noexcept;
    void link_error() noexcept;
    void link_state(ethernet::LinkState state) noexcept;
    void heartbeat() noexcept;
private:
    volatile BringupDiagnostics& boot_;
    volatile LinkDiagnostics& link_;
};
void record_fault(std::uint32_t reason) noexcept;
}
extern "C" {
extern volatile h755::BringupDiagnostics g_bringup;
extern volatile h755::LinkDiagnostics g_link;
}
