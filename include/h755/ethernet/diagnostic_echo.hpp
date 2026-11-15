#pragma once

#include "h755/ethernet/mac_engine.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace h755::ethernet
{
    inline constexpr std::uint16_t diagnostic_ethertype = 0x88B5U;

    enum class DiagnosticResult : std::uint8_t
    {
        ignored,
        sent,
        send_failed
    };

    class DiagnosticEcho final
    {
      public:
        explicit DiagnosticEcho(std::array<std::uint8_t, 6U> address) noexcept : address_{address}
        {
        }
        DiagnosticEcho(const DiagnosticEcho&) = delete;
        DiagnosticEcho& operator=(const DiagnosticEcho&) = delete;

        template <class Send>
        [[nodiscard]] DiagnosticResult on_frame(std::span<const std::byte> frame,
                                                Send&& send) noexcept
        {
            if (attempted_ || frame.size() < 60U || frame.size() > 1514U ||
                frame[12U] != static_cast<std::byte>(diagnostic_ethertype >> 8U) ||
                frame[13U] != static_cast<std::byte>(diagnostic_ethertype & 0xFFU))
            {
                return DiagnosticResult::ignored;
            }
            constexpr char request[] = "H755PING";
            for (std::size_t index = 0U; index < 8U; ++index)
            {
                if (frame[14U + index] != static_cast<std::byte>(request[index]))
                {
                    return DiagnosticResult::ignored;
                }
            }
            bool own_destination = true;
            bool broadcast_destination = true;
            bool zero_source = true;
            bool own_source = true;
            for (std::size_t index = 0U; index < address_.size(); ++index)
            {
                const auto destination = std::to_integer<std::uint8_t>(frame[index]);
                const auto source = std::to_integer<std::uint8_t>(frame[6U + index]);
                own_destination = own_destination && destination == address_[index];
                broadcast_destination = broadcast_destination && destination == 0xFFU;
                zero_source = zero_source && source == 0U;
                own_source = own_source && source == address_[index];
            }
            if ((!own_destination && !broadcast_destination) || zero_source || own_source ||
                (std::to_integer<std::uint8_t>(frame[6U]) & 1U) != 0U)
            {
                return DiagnosticResult::ignored;
            }
            attempted_ = true;
            std::array<std::byte, 60U> reply{};
            for (std::size_t index = 0U; index < address_.size(); ++index)
            {
                reply[index] = frame[6U + index];
                reply[6U + index] = static_cast<std::byte>(address_[index]);
            }
            reply[12U] = static_cast<std::byte>(diagnostic_ethertype >> 8U);
            reply[13U] = static_cast<std::byte>(diagnostic_ethertype & 0xFFU);
            constexpr char response[] = "H755PONG";
            for (std::size_t index = 0U; index < 8U; ++index)
            {
                reply[14U + index] = static_cast<std::byte>(response[index]);
            }
            for (std::size_t index = 0U; index < 4U; ++index)
            {
                reply[22U + index] = frame[22U + index];
            }
            // MAC send copies the reply before this stack buffer goes out of scope.
            const auto outcome = send(std::span<const std::byte>{reply});
            result_ =
                outcome == MacError::none ? DiagnosticResult::sent : DiagnosticResult::send_failed;
            return result_;
        }

        [[nodiscard]] bool attempted() const noexcept
        {
            return attempted_;
        }
        [[nodiscard]] DiagnosticResult result() const noexcept
        {
            return result_;
        }

      private:
        std::array<std::uint8_t, 6U> address_;
        bool attempted_{};
        DiagnosticResult result_{DiagnosticResult::ignored};
    };
}
