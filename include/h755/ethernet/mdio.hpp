#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace h755::ethernet {

template<class Tag>
class Address5 {
public:
    static constexpr std::optional<Address5> from(std::uint32_t value) noexcept
    {
        if (value > 31U) return std::nullopt;
        return Address5{static_cast<std::uint8_t>(value)};
    }
    constexpr std::uint8_t value() const noexcept { return value_; }
private:
    explicit constexpr Address5(std::uint8_t value) noexcept : value_{value} {}
    std::uint8_t value_;
};

struct PhyTag;
struct RegisterTag;
using PhyAddress = Address5<PhyTag>;
using RegisterAddress = Address5<RegisterTag>;

class MdioClock {
public:
    static constexpr std::optional<MdioClock> from_hclk(std::uint32_t hz) noexcept
    {
        if (hz == 0 || hz > 240'000'000U) return std::nullopt;
        if (hz < 35'000'000U) return MdioClock{0x200};
        if (hz < 60'000'000U) return MdioClock{0x300};
        if (hz < 100'000'000U) return MdioClock{0};
        if (hz < 150'000'000U) return MdioClock{0x100};
        return MdioClock{0x400};
    }
    constexpr std::uint32_t bits() const noexcept { return bits_; }
private:
    explicit constexpr MdioClock(std::uint32_t bits) noexcept : bits_{bits} {}
    std::uint32_t bits_;
};

enum class MdioError { busy, invalid_timeout, timeout, no_request, recovery_required, wrong_operation };

template<class Registers>
class MdioTransport {
public:
    MdioTransport(Registers& registers, MdioClock clock) noexcept
        : registers_{registers}, clock_{clock} {}
    MdioTransport(const MdioTransport&) = delete;
    MdioTransport& operator=(const MdioTransport&) = delete;

    std::expected<void, MdioError> begin(PhyAddress phy, RegisterAddress reg,
                                       std::uint32_t now, std::uint32_t timeout) noexcept
    {
        if (auto ready = prepare(now, timeout); !ready) return ready;
        state_ = State::reading;
        registers_.write_address(command(phy, reg) | read_command);
        return {};
    }
    std::expected<void, MdioError> begin_write(PhyAddress phy, RegisterAddress reg, std::uint16_t value,
                                             std::uint32_t now, std::uint32_t timeout) noexcept
    {
        if (auto ready = prepare(now, timeout); !ready) return ready;
        state_ = State::writing;
        // MACMDIODR must be staged before setting MB; neither register may change while busy.
        registers_.write_data(value);
        registers_.write_address(command(phy, reg) | write_command);
        return {};
    }
    std::expected<std::optional<std::uint16_t>, MdioError> poll(std::uint32_t now) noexcept
    {
        const auto complete = completion(State::reading, now);
        if (!complete) return std::unexpected{complete.error()};
        if (!*complete) return std::optional<std::uint16_t>{};
        return static_cast<std::uint16_t>(registers_.read_data() & 0xFFFFU);
    }
    std::expected<bool, MdioError> poll_write(std::uint32_t now) noexcept
    {
        return completion(State::writing, now);
    }
    std::expected<void, MdioError> recover_after_idle() noexcept
    {
        if (state_ != State::fault) return std::unexpected{MdioError::no_request};
        if ((registers_.read_address() & busy_bit) != 0U)
            return std::unexpected{MdioError::busy};
        state_ = State::idle;
        return {};
    }
private:
    enum class State { idle, reading, writing, fault };
    std::expected<void, MdioError> prepare(std::uint32_t now, std::uint32_t timeout) noexcept
    {
        if (timeout == 0U || timeout > 0x7FFFFFFFU) return std::unexpected{MdioError::invalid_timeout};
        if (state_ == State::fault) return std::unexpected{MdioError::recovery_required};
        if (state_ != State::idle || (registers_.read_address() & busy_bit) != 0U)
            return std::unexpected{MdioError::busy};
        started_ = now;
        timeout_ = timeout;
        return {};
    }
    std::uint32_t command(PhyAddress phy, RegisterAddress reg) const noexcept
    {
        return (static_cast<std::uint32_t>(phy.value()) << 21U)
            | (static_cast<std::uint32_t>(reg.value()) << 16U) | clock_.bits() | busy_bit;
    }
    std::expected<bool, MdioError> completion(State expected, std::uint32_t now) noexcept
    {
        if (state_ == State::idle) return std::unexpected{MdioError::no_request};
        if (state_ == State::fault) return std::unexpected{MdioError::recovery_required};
        if (state_ != expected) return std::unexpected{MdioError::wrong_operation};
        if ((registers_.read_address() & busy_bit) == 0U) {
            state_ = State::idle;
            return true;
        }
        // Elapsed time crosses rollover; the caller must not miss a complete tick cycle.
        if (static_cast<std::uint32_t>(now - started_) >= timeout_) {
            state_ = State::fault;
            return std::unexpected{MdioError::timeout};
        }
        return false;
    }
    Registers& registers_;
    MdioClock clock_;
    State state_{State::idle};
    std::uint32_t started_{};
    std::uint32_t timeout_{};
    static constexpr std::uint32_t busy_bit = 1U;
    static constexpr std::uint32_t read_command = 0xCU;
    static constexpr std::uint32_t write_command = 0x4U;
};

constexpr bool is_lan8742a(std::uint16_t id1, std::uint16_t id2) noexcept
{
    return id1 == 0x0007U && (id2 & 0xFFF0U) == 0xC130U;
}

}
