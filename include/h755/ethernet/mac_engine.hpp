#pragma once

#include "h755/ethernet/lan8742.hpp"
#include "h755/ethernet/mac_dma.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace h755::ethernet
{
    inline constexpr std::size_t mac_slots = 4U;
    inline constexpr std::size_t mac_buffer_size = 1536U;

    struct DmaStorage
    {
        alignas(32) std::array<DmaDescriptor, mac_slots> tx{};
        alignas(32) std::array<DmaDescriptor, mac_slots> rx{};
        alignas(32) std::array<std::array<std::byte, mac_buffer_size>, mac_slots> tx_bytes{};
        alignas(32) std::array<std::array<std::byte, mac_buffer_size>, mac_slots> rx_bytes{};
    };

    enum class MacState : std::uint8_t
    {
        cold,
        ready,
        running,
        stopping,
        fault
    };
    enum class MacError : std::uint8_t
    {
        none,
        cache_enabled,
        invalid_storage,
        dma_fault,
        invalid_frame,
        link_down,
        queue_full
    };

    struct MacDiagnostics
    {
        std::uint32_t state;
        std::uint32_t error;
        std::uint32_t received;
        std::uint32_t dropped;
        std::uint32_t transmitted;
        std::uint32_t transmit_errors;
    };

    inline constexpr std::uint32_t dma_tx_stopped = 0x00000002U;
    inline constexpr std::uint32_t dma_rx_stopped = 0x00000100U;
    inline constexpr std::uint32_t dma_rx_unavailable = 0x00000080U;
    inline constexpr std::uint32_t dma_tx_unavailable = 0x00000004U;
    inline constexpr std::uint32_t dma_fatal_bus = 0x00001000U;
    inline constexpr std::uint32_t dma_context_error = 0x00002000U;

    template <class Port> class MacEngine final
    {
      public:
        MacEngine(Port& port, DmaStorage& storage, std::array<std::uint8_t, 6U> address) noexcept
            : port_{port}, storage_{storage}, address_{address}
        {
        }
        MacEngine(const MacEngine&) = delete;
        MacEngine& operator=(const MacEngine&) = delete;

        [[nodiscard]] bool start() noexcept
        {
            if (state_ != MacState::cold)
            {
                return false;
            }
            if (port_.cache_enabled())
            {
                fail(MacError::cache_enabled);
                return false;
            }
            if (!port_.storage_ready(storage_))
            {
                fail(MacError::invalid_storage);
                return false;
            }
            port_.configure(address_, port_.address(storage_.tx.data()),
                            port_.address(storage_.rx.data()), port_.address(storage_.tx.data()),
                            port_.address(&storage_.rx[mac_slots - 1U]));
            state_ = MacState::ready;
            return true;
        }

        void poll(std::uint32_t now, LinkState link) noexcept
        {
            if (state_ == MacState::stopping)
            {
                if (port_.channels_stopped())
                {
                    for (std::size_t index = 0U; index < mac_slots; ++index)
                    {
                        if (!tx_busy_[index])
                        {
                            continue;
                        }
                        if (transmit_success(storage_.tx[index]))
                        {
                            ++transmitted_;
                        }
                        else
                        {
                            ++transmit_errors_;
                        }
                        tx_busy_[index] = false;
                    }
                    port_.clear_status(dma_tx_stopped | dma_rx_stopped);
                    state_ = MacState::ready;
                }
                else if (now - stopping_started_ >= 50U)
                {
                    fail(MacError::dma_fault);
                }
                return;
            }
            if (state_ != MacState::ready && state_ != MacState::running)
            {
                return;
            }
            if ((port_.status() & (dma_fatal_bus | dma_context_error)) != 0U)
            {
                fail(MacError::dma_fault);
                return;
            }
            if (state_ == MacState::running)
            {
                poll_frames();
            }
            if (active_link(link) && state_ == MacState::ready)
            {
                start_link(link);
            }
            else if (state_ == MacState::running && link != running_mode_)
            {
                stop_link(now);
            }
        }

        [[nodiscard]] MacError send(std::span<const std::byte> frame) noexcept
        {
            // The caller must not borrow a buffer that DMA currently owns.
            if (state_ != MacState::running)
            {
                return MacError::link_down;
            }
            if (frame.size() < 60U || frame.size() > mac_buffer_size)
            {
                return MacError::invalid_frame;
            }
            const auto index = tx_next_;
            if (tx_busy_[index])
            {
                return MacError::queue_full;
            }
            for (std::size_t offset = 0U; offset < frame.size(); ++offset)
            {
                storage_.tx_bytes[index][offset] = frame[offset];
            }
            const auto encoded = tx_descriptor(port_.address(storage_.tx_bytes[index].data()),
                                               frame.size(), mac_buffer_size);
            if (!encoded)
            {
                return MacError::invalid_frame;
            }
            volatile auto& descriptor = storage_.tx[index];
            descriptor.word0 = encoded->word0;
            descriptor.word1 = encoded->word1;
            descriptor.word2 = encoded->word2;
            port_.barrier();
            descriptor.word3 = encoded->word3;
            port_.complete_barrier();
            tx_busy_[index] = true;
            tx_next_ = (index + 1U) % mac_slots;
            port_.tx_tail(port_.address(&storage_.tx[tx_next_]));
            return MacError::none;
        }

        [[nodiscard]] MacState state() const noexcept
        {
            return state_;
        }
        [[nodiscard]] MacError error() const noexcept
        {
            return error_;
        }
        [[nodiscard]] std::uint32_t received() const noexcept
        {
            return received_;
        }
        [[nodiscard]] std::uint32_t dropped() const noexcept
        {
            return dropped_;
        }
        [[nodiscard]] std::uint32_t transmitted() const noexcept
        {
            return transmitted_;
        }
        [[nodiscard]] std::uint32_t transmit_errors() const noexcept
        {
            return transmit_errors_;
        }

      private:
        static bool active_link(LinkState link) noexcept
        {
            return link == LinkState::mb10_half || link == LinkState::mb10_full ||
                   link == LinkState::mb100_half || link == LinkState::mb100_full;
        }

        void start_link(LinkState link) noexcept
        {
            tx_next_ = 0U;
            rx_next_ = 0U;
            for (std::size_t index = 0U; index < mac_slots; ++index)
            {
                tx_busy_[index] = false;
                volatile auto& tx = storage_.tx[index];
                tx.word0 = 0U;
                tx.word1 = 0U;
                tx.word2 = 0U;
                tx.word3 = 0U;
                const auto encoded = rx_descriptor(port_.address(storage_.rx_bytes[index].data()));
                volatile auto& rx = storage_.rx[index];
                rx.word0 = encoded.word0;
                rx.word1 = encoded.word1;
                rx.word2 = encoded.word2;
                port_.barrier();
                rx.word3 = encoded.word3;
            }
            port_.complete_barrier();
            port_.start_link(link);
            port_.clear_status(dma_tx_stopped | dma_rx_stopped | dma_rx_unavailable |
                               dma_tx_unavailable);
            running_mode_ = link;
            state_ = MacState::running;
        }

        void stop_link(std::uint32_t now) noexcept
        {
            port_.stop_link();
            stopping_started_ = now;
            state_ = MacState::stopping;
        }

        void poll_frames() noexcept
        {
            for (std::size_t index = 0U; index < mac_slots; ++index)
            {
                if (tx_busy_[index] && transmit_complete(storage_.tx[index]))
                {
                    tx_busy_[index] = false;
                    if (transmit_success(storage_.tx[index]))
                    {
                        ++transmitted_;
                    }
                    else
                    {
                        ++transmit_errors_;
                    }
                }
            }
            for (std::size_t scanned = 0U; scanned < mac_slots; ++scanned)
            {
                volatile auto& descriptor = storage_.rx[rx_next_];
                if ((descriptor.word3 & descriptor_own) != 0U)
                {
                    break;
                }
                port_.barrier();
                if (received_length(descriptor, mac_buffer_size))
                {
                    ++received_;
                }
                else
                {
                    ++dropped_;
                }
                const auto encoded =
                    rx_descriptor(port_.address(storage_.rx_bytes[rx_next_].data()));
                descriptor.word0 = encoded.word0;
                descriptor.word1 = encoded.word1;
                descriptor.word2 = encoded.word2;
                port_.barrier();
                descriptor.word3 = encoded.word3;
                port_.complete_barrier();
                port_.rx_tail(port_.address(&storage_.rx[rx_next_]));
                rx_next_ = (rx_next_ + 1U) % mac_slots;
            }
            if ((port_.status() & dma_rx_unavailable) != 0U)
            {
                port_.clear_status(dma_rx_unavailable);
                port_.rx_tail(port_.address(&storage_.rx[(rx_next_ + mac_slots - 1U) % mac_slots]));
            }
        }

        void fail(MacError error) noexcept
        {
            port_.stop_link();
            error_ = error;
            state_ = MacState::fault;
        }

        Port& port_;
        DmaStorage& storage_;
        std::array<std::uint8_t, 6U> address_;
        std::array<bool, mac_slots> tx_busy_{};
        std::uint32_t received_{};
        std::uint32_t dropped_{};
        std::uint32_t transmitted_{};
        std::uint32_t transmit_errors_{};
        std::uint32_t stopping_started_{};
        std::size_t tx_next_{};
        std::size_t rx_next_{};
        LinkState running_mode_{LinkState::down};
        MacState state_{MacState::cold};
        MacError error_{MacError::none};
    };
}
