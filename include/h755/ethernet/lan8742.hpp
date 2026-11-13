#pragma once
#include "h755/ethernet/mdio.hpp"

namespace h755::ethernet {
enum class LinkState { down, negotiating, mb10_half, mb10_full, mb100_half, mb100_full };
enum class LinkError { busy, invalid_timeout, no_request, transport, invalid_status };

template<class Mdio>
class Lan8742LinkMonitor {
public:
    Lan8742LinkMonitor(Mdio& mdio, PhyAddress phy) noexcept : mdio_{mdio}, phy_{phy} {}
    Lan8742LinkMonitor(const Lan8742LinkMonitor&) = delete;
    Lan8742LinkMonitor& operator=(const Lan8742LinkMonitor&) = delete;
    std::expected<void, LinkError> begin(std::uint32_t now, std::uint32_t timeout) noexcept
    {
        if (step_ != Step::idle) return std::unexpected{LinkError::busy};
        if (timeout == 0U || timeout > 0x7FFFFFFFU) return std::unexpected{LinkError::invalid_timeout};
        timeout_ = timeout;
        return request(Step::latch, 1U, now);
    }
    std::expected<std::optional<LinkState>, LinkError> poll(std::uint32_t now) noexcept
    {
        if (step_ == Step::idle) return std::unexpected{LinkError::no_request};
        const auto response = mdio_.poll(now);
        if (!response) return fail(LinkError::transport);
        if (!response->has_value()) return std::optional<LinkState>{};
        const auto value = **response;
        if (value == 0xFFFFU || ((step_ == Step::status || step_ == Step::verify) && (value & 0x10U) != 0U)) return fail(LinkError::invalid_status);
        switch (step_) {
        case Step::latch:
            // BMSR link-down is latched low; only the second read reports current link.
            return advance(Step::status, 1U, now);
        case Step::status:
            if ((value & 4U) == 0U) return finish(LinkState::down);
            return advance(Step::control, 0U, now);
        case Step::control:
            if ((value & 0xCC00U) != 0U) return finish(LinkState::down);
            if ((value & 0x1000U) != 0U) return advance(Step::resolved, 31U, now);
            mode_ = (value & 0x2000U) != 0U
                ? ((value & 0x100U) != 0U ? LinkState::mb100_full : LinkState::mb100_half)
                : ((value & 0x100U) != 0U ? LinkState::mb10_full : LinkState::mb10_half);
            return advance(Step::verify, 1U, now);
        case Step::resolved:
            if ((value & 0x1000U) == 0U) return finish(LinkState::negotiating);
            switch (value & 0x1CU) {
            case 0x04U: mode_ = LinkState::mb10_half; break;
            case 0x14U: mode_ = LinkState::mb10_full; break;
            case 0x08U: mode_ = LinkState::mb100_half; break;
            case 0x18U: mode_ = LinkState::mb100_full; break;
            default: return fail(LinkError::invalid_status);
            }
            return advance(Step::verify, 1U, now);
        case Step::verify:
            return finish((value & 4U) != 0U ? mode_ : LinkState::down);
        case Step::idle: break;
        }
        return fail(LinkError::no_request);
    }
private:
    enum class Step { idle, latch, status, control, resolved, verify };
    std::expected<void, LinkError> request(Step step, std::uint32_t reg, std::uint32_t now) noexcept
    {
        const auto address = RegisterAddress::from(reg);
        if (!address || !mdio_.begin(phy_, *address, now, timeout_)) {
            step_ = Step::idle;
            return std::unexpected{LinkError::transport};
        }
        step_ = step;
        return {};
    }
    std::expected<std::optional<LinkState>, LinkError> advance(Step step, std::uint32_t reg, std::uint32_t now) noexcept
    {
        if (auto started = request(step, reg, now); !started) return std::unexpected{started.error()};
        return std::optional<LinkState>{};
    }
    std::expected<std::optional<LinkState>, LinkError> finish(LinkState state) noexcept
    { step_ = Step::idle; return state; }
    std::expected<std::optional<LinkState>, LinkError> fail(LinkError error) noexcept
    { step_ = Step::idle; return std::unexpected{error}; }
    Mdio& mdio_;
    PhyAddress phy_;
    Step step_{Step::idle};
    LinkState mode_{LinkState::down};
    std::uint32_t timeout_{};
};
}
