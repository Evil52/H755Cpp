#include "h755/diagnostics.hpp"
#include "h755/ethernet/phy_control.hpp"
#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<h755::BringupDiagnostics>);
static_assert(sizeof(h755::BringupDiagnostics) == 40);
static_assert(offsetof(h755::BringupDiagnostics, fault) == 32);
static_assert(sizeof(h755::LinkDiagnostics) == 12);
static_assert(sizeof(h755::PhyDiagnostics) == 28);
extern "C" {
volatile h755::BringupDiagnostics g_bringup{};
volatile h755::LinkDiagnostics g_link{0xFFFFFFFFU, 0U, 0U};
volatile h755::PhyDiagnostics g_phy{};
}
namespace h755 {
void Diagnostics::begin(std::uint32_t hz) noexcept
{
    boot_.magic = 0x48373535U;
    boot_.state = 1U;
    boot_.hclk_hz = hz;
    boot_.phy_address = 0xFFFFFFFFU;
    phy_.state = static_cast<std::uint32_t>(ethernet::PhyState::idle);
    phy_.error = 0U;
    phy_.attempts = 0U;
    phy_.completed = 0U;
    phy_.startup_bmcr = 0U;
    phy_.startup_bmsr = 0U;
    phy_.startup_mode = 0U;
}
void Diagnostics::parked(std::uint32_t marker) noexcept
{
    boot_.m4_marker = marker;
    boot_.state = 2U;
}
void Diagnostics::scanning() noexcept { boot_.state = 3U; }
void Diagnostics::found(std::uint32_t address, std::uint16_t id1, std::uint16_t id2) noexcept
{
    boot_.phy_address = address;
    boot_.phy_id1 = id1;
    boot_.phy_id2 = id2;
}
void Diagnostics::finish_scan(bool found) noexcept { boot_.state = found ? 4U : 5U; }
void Diagnostics::mdio_error() noexcept { boot_.mdio_errors = boot_.mdio_errors + 1U; }
void Diagnostics::link_error() noexcept
{
    link_.state = 0xFFFFFFFFU;
    link_.errors = link_.errors + 1U;
}
void Diagnostics::link_state(ethernet::LinkState state) noexcept
{
    link_.state = static_cast<std::uint32_t>(state);
    link_.samples = link_.samples + 1U;
}
void Diagnostics::heartbeat() noexcept { boot_.heartbeat = boot_.heartbeat + 1U; }
void Diagnostics::phy_begin() noexcept
{
    phy_.state = static_cast<std::uint32_t>(ethernet::PhyState::resetting);
    phy_.error = 0U;
    phy_.attempts = phy_.attempts + 1U;
}
void Diagnostics::phy_progress(ethernet::PhyState state) noexcept { phy_.state = static_cast<std::uint32_t>(state); }
void Diagnostics::phy_failed(ethernet::PhyError error) noexcept
{
    phy_.state = static_cast<std::uint32_t>(ethernet::PhyState::failed);
    phy_.error = static_cast<std::uint32_t>(error);
}
void Diagnostics::phy_ready() noexcept
{
    phy_.state = static_cast<std::uint32_t>(ethernet::PhyState::ready);
    phy_.error = 0U;
    phy_.completed = phy_.completed + 1U;
}
void Diagnostics::phy_registers(std::uint16_t bmcr, std::uint16_t bmsr, std::uint16_t resolved) noexcept
{
    phy_.startup_bmcr = bmcr;
    phy_.startup_bmsr = bmsr;
    phy_.startup_mode = resolved;
}
void record_fault(std::uint32_t reason) noexcept
{
    g_bringup.fault = reason;
    g_bringup.state = 0xFFFFFFFFU;
}
}
