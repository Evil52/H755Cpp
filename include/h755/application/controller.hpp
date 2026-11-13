#pragma once
#include "h755/diagnostics.hpp"
#include "h755/fault.hpp"
#include <expected>
#include <optional>

namespace h755 {
template<class Board, class Clock, class Mdio>
class Application {
public:
    Application(Board& board, Clock& clock, Mdio& mdio, Diagnostics& diagnostics) noexcept
        : board_{board}, clock_{clock}, mdio_{mdio}, diagnostics_{diagnostics} {}
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    std::expected<void, FaultCode> start() noexcept
    {
        if (lifecycle_ != Lifecycle::cold) return std::unexpected{FaultCode::invalid_state};
        lifecycle_ = Lifecycle::starting;
        const auto hz = board_.hclk_hz();
        diagnostics_.begin(hz);
        if (hz != 64'000'000U) return fail_start(FaultCode::clock);
        board_.configure();
        if (!clock_.start(hz)) return fail_start(FaultCode::timebase);
        const auto began = clock_.now();
        while (board_.m4_marker() != 0x5041524BU) {
            if (clock_.now() - began >= 100U) return fail_start(FaultCode::m4_timeout);
            board_.idle();
        }
        diagnostics_.parked(board_.m4_marker());
        // PHY power-on reset settles while SysTick continues to advance.
        while (clock_.now() - began < 200U) board_.idle();
        discover_phy();
        board_.red(!monitor_.has_value());
        diagnostics_.finish_scan(monitor_.has_value());
        last_link_ = clock_.now();
        last_heartbeat_ = last_link_;
        lifecycle_ = Lifecycle::running;
        return {};
    }

    void poll() noexcept
    {
        if (lifecycle_ != Lifecycle::running) return;
        const auto now = clock_.now();
        if (monitor_ && !active_ && now - last_link_ >= 250U) {
            last_link_ = now;
            active_ = monitor_->begin(now, 20U).has_value();
            if (!active_) link_failure();
        }
        if (active_) poll_link(now);
        if (now - last_heartbeat_ >= 3000U) {
            last_heartbeat_ = now;
            lit_ = !lit_;
            board_.green(lit_);
            diagnostics_.heartbeat();
        }
    }
private:
    enum class Lifecycle { cold, starting, running, failed };
    std::expected<void, FaultCode> fail_start(FaultCode error) noexcept
    {
        lifecycle_ = Lifecycle::failed;
        return std::unexpected{error};
    }

    std::expected<std::uint16_t, ethernet::MdioError> read_phy(ethernet::PhyAddress phy, std::uint32_t reg) noexcept
    {
        const auto address = ethernet::RegisterAddress::from(reg);
        if (!address) return std::unexpected{ethernet::MdioError::no_request};
        if (auto begun = mdio_.begin(phy, *address, clock_.now(), 20U); !begun)
            return std::unexpected{begun.error()};
        for (;;) {
            const auto result = mdio_.poll(clock_.now());
            if (!result) return std::unexpected{result.error()};
            if (result->has_value()) return **result;
            board_.idle();
        }
    }

    void discover_phy() noexcept
    {
        diagnostics_.scanning();
        for (std::uint32_t address = 0; address < 32U; ++address) {
            const auto phy = ethernet::PhyAddress::from(address);
            if (!phy) return;
            const auto id1 = read_phy(*phy, 2U);
            const auto id2 = read_phy(*phy, 3U);
            if (!id1 || !id2) {
                diagnostics_.mdio_error();
                if (!mdio_.recover_after_idle()) return;
            } else if (ethernet::is_lan8742a(*id1, *id2)) {
                diagnostics_.found(address, *id1, *id2);
                monitor_.emplace(mdio_, *phy);
                return;
            }
        }
    }

    void poll_link(std::uint32_t now) noexcept
    {
        const auto result = monitor_->poll(now);
        if (!result) {
            link_failure();
        } else if (result->has_value()) {
            active_ = false;
            diagnostics_.link_state(**result);
        }
    }

    void link_failure() noexcept
    {
        active_ = false;
        diagnostics_.link_error();
        (void)mdio_.recover_after_idle();
    }

    Board& board_;
    Clock& clock_;
    Mdio& mdio_;
    Diagnostics& diagnostics_;
    std::optional<ethernet::Lan8742LinkMonitor<Mdio>> monitor_;
    std::uint32_t last_link_{};
    std::uint32_t last_heartbeat_{};
    Lifecycle lifecycle_{Lifecycle::cold};
    bool active_{}, lit_{};
};
}
