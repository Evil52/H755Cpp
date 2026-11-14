#pragma once
#include "h755/ethernet/lan8742.hpp"

namespace h755::ethernet {
enum class PhyError { busy = 1, invalid_timing, no_request, transport, reset_timeout, negotiation_timeout, invalid_status, configuration_timeout };
enum class PhyState { idle, resetting, negotiating, ready, failed, waiting_link };
struct PhyTiming {
    std::uint32_t transaction_ms{20};
    std::uint32_t reset_ms{600};
    std::uint32_t negotiation_ms{5000};
    std::uint32_t poll_ms{10};
};
struct PhySnapshot {
    std::uint16_t bmcr{};
    std::uint16_t bmsr{};
    std::uint16_t resolved{};
};
template<class Mdio>
class Lan8742Control {
public:
    Lan8742Control(Mdio& mdio, PhyAddress phy) noexcept : mdio_{mdio}, phy_{phy} {}
    Lan8742Control(const Lan8742Control&) = delete;
    Lan8742Control& operator=(const Lan8742Control&) = delete;
    std::expected<void, PhyError> begin(std::uint32_t now, const PhyTiming& timing = {}) noexcept
    {
        if (step_ != Step::idle) return std::unexpected{PhyError::busy};
        if (!valid(timing)) return std::unexpected{PhyError::invalid_timing};
        timing_ = timing;
        negotiation_verified_ = false;
        snapshot_ = {};
        phase_started_ = now;
        state_ = PhyState::resetting;
        step_ = Step::reset_write;
        const auto result = request(now);
        if (!result) { stop(); return std::unexpected{result.error()}; }
        return {};
    }
    std::expected<std::optional<LinkState>, PhyError> poll(std::uint32_t now) noexcept
    {
        if (step_ == Step::idle) return std::unexpected{PhyError::no_request};
        if (!pending_) {
            if (expired(now)) return fail(deadline_error());
            if (static_cast<std::uint32_t>(now - last_completed_) < timing_.poll_ms) return std::optional<LinkState>{};
            if (auto issued = request(now); !issued) return fail(issued.error());
            return std::optional<LinkState>{};
        }
        const auto response = receive(now);
        if (!response) return fail(response.error());
        if (!response->has_value()) return std::optional<LinkState>{};
        pending_ = false;
        if (expired(now)) return fail(deadline_error());
        return advance(**response, now);
    }
    PhyState state() const noexcept { return state_; }
    PhySnapshot snapshot() const noexcept { return snapshot_; }
private:
    enum class Step { idle, reset_write, reset_read, negotiation_write, control, latch, status, mode, verify };
    static bool valid(const PhyTiming& timing) noexcept
    {
        const auto bounded = [](std::uint32_t value) { return value > 0U && value <= 0x7FFFFFFFU; };
        return bounded(timing.transaction_ms) && bounded(timing.reset_ms) && bounded(timing.negotiation_ms)
            && bounded(timing.poll_ms) && timing.poll_ms <= timing.reset_ms && timing.poll_ms <= timing.negotiation_ms;
    }
    std::uint32_t budget() const noexcept { return state_ == PhyState::resetting ? timing_.reset_ms : timing_.negotiation_ms; }
    bool expired(std::uint32_t now) const noexcept { return static_cast<std::uint32_t>(now - phase_started_) >= budget(); }
    PhyError deadline_error() const noexcept
    {
        if (state_ == PhyState::resetting) return PhyError::reset_timeout;
        return negotiation_verified_ ? PhyError::negotiation_timeout : PhyError::configuration_timeout;
    }
    bool writing() const noexcept { return step_ == Step::reset_write || step_ == Step::negotiation_write; }
    std::expected<void, PhyError> request(std::uint32_t now) noexcept
    {
        if (expired(now)) return std::unexpected{deadline_error()};
        const auto remaining = budget() - static_cast<std::uint32_t>(now - phase_started_);
        const auto timeout = remaining < timing_.transaction_ms ? remaining : timing_.transaction_ms;
        const auto reg = step_ == Step::mode ? 31U :
            (step_ == Step::latch || step_ == Step::status || step_ == Step::verify ? 1U : 0U);
        const auto address = RegisterAddress::from(reg);
        if (!address) return std::unexpected{PhyError::invalid_status};
        // LAN8742 forbids setting other BMCR bits with soft reset; AN starts only after reset clears.
        const auto issued = writing()
            ? mdio_.begin_write(phy_, *address, static_cast<std::uint16_t>(step_ == Step::reset_write ? 0x8000U : 0x1200U), now, timeout)
            : mdio_.begin(phy_, *address, now, timeout);
        if (!issued) return std::unexpected{PhyError::transport};
        pending_ = true;
        return {};
    }
    std::expected<std::optional<std::uint16_t>, PhyError> receive(std::uint32_t now) noexcept
    {
        if (writing()) {
            const auto result = mdio_.poll_write(now);
            if (!result) return std::unexpected{PhyError::transport};
            return *result ? std::optional<std::uint16_t>{0U} : std::optional<std::uint16_t>{};
        }
        const auto result = mdio_.poll(now);
        if (!result) return std::unexpected{PhyError::transport};
        return *result;
    }
    std::expected<std::optional<LinkState>, PhyError> schedule(Step next, std::uint32_t now) noexcept
    {
        step_ = next;
        last_completed_ = now;
        return std::optional<LinkState>{};
    }
    std::expected<std::optional<LinkState>, PhyError> advance(std::uint16_t value, std::uint32_t now) noexcept
    {
        if (value == 0xFFFFU && step_ != Step::reset_read) return fail(PhyError::invalid_status);
        switch (step_) {
        case Step::reset_write: return schedule(Step::reset_read, now);
        case Step::reset_read:
            snapshot_.bmcr = value;
            // A non-responding PHY can read all ones during reset; it must never look ready.
            if ((value & 0x8000U) != 0U) return schedule(Step::reset_read, now);
            state_ = PhyState::negotiating;
            phase_started_ = now;
            return schedule(Step::negotiation_write, now);
        case Step::negotiation_write: return schedule(Step::control, now);
        case Step::control: return control(value, now);
        case Step::latch: return schedule(Step::status, now);
        case Step::status:
        case Step::verify: return status(value, now);
        case Step::mode: return mode(value, now);
        case Step::idle: return fail(PhyError::invalid_status);
        }
        return fail(PhyError::invalid_status);
    }
    std::expected<std::optional<LinkState>, PhyError> control(std::uint16_t value, std::uint32_t now) noexcept
    {
        snapshot_.bmcr = value;
        if ((value & 0xCC00U) != 0U || (value & 0x1000U) == 0U) return fail(PhyError::invalid_status);
        if ((value & 0x200U) != 0U) return schedule(Step::control, now);
        negotiation_verified_ = true;
        return schedule(Step::latch, now);
    }
    std::expected<std::optional<LinkState>, PhyError> status(std::uint16_t value, std::uint32_t now) noexcept
    {
        snapshot_.bmsr = value;
        if ((value & 0x10U) != 0U) return fail(PhyError::invalid_status);
        if ((value & 0x24U) != 0x24U) return schedule(Step::latch, now);
        if (step_ != Step::verify) return schedule(Step::mode, now);
        step_ = Step::idle;
        state_ = PhyState::ready;
        return mode_;
    }
    std::expected<std::optional<LinkState>, PhyError> mode(std::uint16_t value, std::uint32_t now) noexcept
    {
        snapshot_.resolved = value;
        const auto decoded = decode_lan8742_mode(value);
        if (!decoded) return fail(PhyError::invalid_status);
        if (*decoded == LinkState::negotiating) return schedule(Step::latch, now);
        mode_ = *decoded;
        return schedule(Step::verify, now);
    }
    void stop() noexcept { step_ = Step::idle; pending_ = false; state_ = PhyState::failed; }
    std::expected<std::optional<LinkState>, PhyError> fail(PhyError error) noexcept
    {
        stop();
        return std::unexpected{error};
    }
    Mdio& mdio_;
    PhyAddress phy_;
    PhyTiming timing_{};
    PhySnapshot snapshot_{};
    PhyState state_{PhyState::idle};
    Step step_{Step::idle};
    LinkState mode_{LinkState::down};
    std::uint32_t phase_started_{};
    std::uint32_t last_completed_{};
    bool pending_{};
    bool negotiation_verified_{};
};
}
