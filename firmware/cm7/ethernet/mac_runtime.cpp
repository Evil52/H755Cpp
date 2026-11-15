#include "mac_runtime.hpp"
#include <bit>

extern "C"
{
    volatile h755::ethernet::MacDiagnostics g_mac{};
}

namespace h755::ethernet
{
    static_assert(dma_tx_stopped == ETH_DMACSR_TPS);
    static_assert(dma_rx_stopped == ETH_DMACSR_RPS);
    static_assert(ETH_DMADSR_TPS_STOPPED == 0U);
    static_assert(ETH_DMADSR_RPS_STOPPED == 0U);
    static_assert(dma_rx_unavailable == ETH_DMACSR_RBU);
    static_assert(dma_tx_unavailable == ETH_DMACSR_TBU);
    static_assert(dma_fatal_bus == ETH_DMACSR_FBE);
    static_assert(dma_context_error == ETH_DMACSR_CDE);
    static_assert(sizeof(DmaStorage) <= 128U * 1024U);
    static_assert(mac_buffer_size <= (ETH_DMACRCR_RBSZ_Msk >> ETH_DMACRCR_RBSZ_Pos));

    bool CmsisMacPort::cache_enabled() const noexcept
    {
        return (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
    }

    bool CmsisMacPort::storage_ready(const DmaStorage& storage) const noexcept
    {
        const auto start = address(&storage);
        return (start & 31U) == 0U &&
               inside_dma_region(start, sizeof(storage), 0x30000000U, 128U * 1024U);
    }

    std::uint32_t CmsisMacPort::address(const void* pointer) const noexcept
    {
        static_assert(sizeof(std::uintptr_t) == sizeof(std::uint32_t));
        return std::bit_cast<std::uint32_t>(pointer);
    }

    void CmsisMacPort::configure(std::array<std::uint8_t, 6U> mac, std::uint32_t tx_base,
                                 std::uint32_t rx_base, std::uint32_t tx_tail,
                                 std::uint32_t rx_tail) noexcept
    {
        // Board::configure reset ETH through RCC before PHY MDIO began.
        registers_.MACCR = 0U;
        registers_.MACPFR = 0U;
        registers_.MACA0HR = (static_cast<std::uint32_t>(mac[5U]) << 8U) | mac[4U];
        registers_.MACA0LR = (static_cast<std::uint32_t>(mac[3U]) << 24U) |
                             (static_cast<std::uint32_t>(mac[2U]) << 16U) |
                             (static_cast<std::uint32_t>(mac[1U]) << 8U) | mac[0U];
        registers_.MTLTQOMR = ETH_MTLTQOMR_TSF;
        registers_.MTLRQOMR = ETH_MTLRQOMR_RSF;
        registers_.DMACCR = ETH_DMACCR_DSL_0BIT;
        registers_.DMACRCR = static_cast<std::uint32_t>(mac_buffer_size) << ETH_DMACRCR_RBSZ_Pos;
        registers_.DMACIER = 0U;
        registers_.DMACTDLAR = tx_base;
        registers_.DMACRDLAR = rx_base;
        registers_.DMACTDRLR = static_cast<std::uint32_t>(mac_slots - 1U);
        registers_.DMACRDRLR = static_cast<std::uint32_t>(mac_slots - 1U);
        registers_.DMACTDTPR = tx_tail;
        registers_.DMACRDTPR = rx_tail;
    }

    void CmsisMacPort::start_link(LinkState link) noexcept
    {
        registers_.MACCR =
            (link == LinkState::mb100_half || link == LinkState::mb100_full ? ETH_MACCR_FES : 0U) |
            (link == LinkState::mb10_full || link == LinkState::mb100_full ? ETH_MACCR_DM : 0U) |
            ETH_MACCR_TE | ETH_MACCR_RE;
        registers_.MTLTQOMR = registers_.MTLTQOMR | ETH_MTLTQOMR_FTQ;
        registers_.DMACTCR = registers_.DMACTCR | ETH_DMACTCR_ST;
        registers_.DMACRCR = registers_.DMACRCR | ETH_DMACRCR_SR;
    }

    void CmsisMacPort::stop_link() noexcept
    {
        registers_.DMACTCR = registers_.DMACTCR & ~ETH_DMACTCR_ST;
        registers_.DMACRCR = registers_.DMACRCR & ~ETH_DMACRCR_SR;
        registers_.MACCR = registers_.MACCR & ~(ETH_MACCR_TE | ETH_MACCR_RE);
    }

    bool CmsisMacPort::channels_stopped() const noexcept
    {
        return (registers_.DMADSR & (ETH_DMADSR_TPS | ETH_DMADSR_RPS)) == 0U;
    }

    std::uint32_t CmsisMacPort::status() const noexcept
    {
        return registers_.DMACSR;
    }
    void CmsisMacPort::clear_status(std::uint32_t bits) noexcept
    {
        registers_.DMACSR = bits;
    }
    void CmsisMacPort::tx_tail(std::uint32_t address) noexcept
    {
        registers_.DMACTDTPR = address;
    }
    void CmsisMacPort::rx_tail(std::uint32_t address) noexcept
    {
        registers_.DMACRDTPR = address;
    }
    void CmsisMacPort::barrier() const noexcept
    {
        __DMB();
    }
    void CmsisMacPort::complete_barrier() const noexcept
    {
        __DSB();
    }
}
