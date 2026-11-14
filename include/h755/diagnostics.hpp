#pragma once
#include "h755/ethernet/lan8742.hpp"
#include <cstdint>

namespace h755::ethernet { enum class PhyState; enum class PhyError; }

namespace h755 {
struct BringupDiagnostics {
    std::uint32_t magic, state, heartbeat, hclk_hz, phy_address;
    std::uint32_t phy_id1, phy_id2, mdio_errors, fault, m4_marker;
};
struct LinkDiagnostics { std::uint32_t state, samples, errors; };
struct PhyDiagnostics { std::uint32_t state, error, attempts, completed, startup_bmcr, startup_bmsr, startup_mode; };

class Diagnostics {
public:
    Diagnostics(volatile BringupDiagnostics& boot, volatile LinkDiagnostics& link, volatile PhyDiagnostics& phy) noexcept
        : boot_{boot}, link_{link}, phy_{phy} {}
    void begin(std::uint32_t hz) noexcept;
    void parked(std::uint32_t marker) noexcept;
    void scanning() noexcept;
    void found(std::uint32_t address, std::uint16_t id1, std::uint16_t id2) noexcept;
    void finish_scan(bool found) noexcept;
    void mdio_error() noexcept;
    void link_error() noexcept;
    void link_state(ethernet::LinkState state) noexcept;
    void heartbeat() noexcept;
    void phy_begin() noexcept;
    void phy_progress(ethernet::PhyState state) noexcept;
    void phy_failed(ethernet::PhyError error) noexcept;
    void phy_ready() noexcept;
    void phy_registers(std::uint16_t bmcr, std::uint16_t bmsr, std::uint16_t resolved) noexcept;
private:
    volatile BringupDiagnostics& boot_;
    volatile LinkDiagnostics& link_;
    volatile PhyDiagnostics& phy_;
};
void record_fault(std::uint32_t reason) noexcept;
}
extern "C" {
extern volatile h755::BringupDiagnostics g_bringup;
extern volatile h755::LinkDiagnostics g_link;
extern volatile h755::PhyDiagnostics g_phy;
}
