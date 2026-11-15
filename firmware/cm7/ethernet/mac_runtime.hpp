#pragma once

#include "h755/ethernet/mac_engine.hpp"
#include "stm32h7xx.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace h755::ethernet
{
    class CmsisMacPort final
    {
      public:
        explicit CmsisMacPort(ETH_TypeDef& registers) noexcept : registers_{registers}
        {
        }
        [[nodiscard]] bool cache_enabled() const noexcept;
        [[nodiscard]] bool storage_ready(const DmaStorage& storage) const noexcept;
        [[nodiscard]] std::uint32_t address(const void* pointer) const noexcept;
        void configure(std::array<std::uint8_t, 6U> mac, std::uint32_t tx_base,
                       std::uint32_t rx_base, std::uint32_t tx_tail,
                       std::uint32_t rx_tail) noexcept;
        void start_link(LinkState link) noexcept;
        void stop_link() noexcept;
        [[nodiscard]] bool channels_stopped() const noexcept;
        [[nodiscard]] std::uint32_t status() const noexcept;
        void clear_status(std::uint32_t bits) noexcept;
        void tx_tail(std::uint32_t address) noexcept;
        void rx_tail(std::uint32_t address) noexcept;
        void barrier() const noexcept;
        void complete_barrier() const noexcept;

      private:
        ETH_TypeDef& registers_;
    };

    using MacRuntime = MacEngine<CmsisMacPort>;
}

extern "C" volatile h755::ethernet::MacDiagnostics g_mac;
