#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace h755::ethernet
{
    struct DmaDescriptor
    {
        std::uint32_t word0;
        std::uint32_t word1;
        std::uint32_t word2;
        std::uint32_t word3;
    };

    static_assert(sizeof(DmaDescriptor) == 16U);
    static_assert(alignof(DmaDescriptor) == 4U);

    inline constexpr std::uint32_t descriptor_own = 0x80000000U;
    inline constexpr std::uint32_t descriptor_context = 0x40000000U;
    inline constexpr std::uint32_t descriptor_first = 0x20000000U;
    inline constexpr std::uint32_t descriptor_last = 0x10000000U;
    inline constexpr std::uint32_t descriptor_error = 0x00008000U;
    inline constexpr std::uint32_t rx_buffer1_valid = 0x01000000U;
    inline constexpr std::uint32_t frame_length_mask = 0x00007FFFU;

    [[nodiscard]] inline DmaDescriptor rx_descriptor(std::uint32_t address) noexcept
    {
        return {address, 0U, 0U, descriptor_own | rx_buffer1_valid};
    }

    [[nodiscard]] inline std::optional<std::size_t>
    received_length(const volatile DmaDescriptor& descriptor, std::size_t capacity) noexcept
    {
        const auto status = descriptor.word3;
        if ((status & (descriptor_own | descriptor_context | descriptor_error)) != 0U ||
            (status & (descriptor_first | descriptor_last)) != (descriptor_first | descriptor_last))
        {
            return std::nullopt;
        }
        const auto length = static_cast<std::size_t>(status & frame_length_mask);
        return length != 0U && length <= capacity ? std::optional{length} : std::nullopt;
    }

    [[nodiscard]] inline std::optional<DmaDescriptor>
    tx_descriptor(std::uint32_t address, std::size_t length, std::size_t capacity) noexcept
    {
        if (length == 0U || length > capacity || length > frame_length_mask)
        {
            return std::nullopt;
        }
        return DmaDescriptor{address, 0U, static_cast<std::uint32_t>(length),
                             descriptor_own | descriptor_first | descriptor_last |
                                 static_cast<std::uint32_t>(length)};
    }

    [[nodiscard]] inline bool transmit_complete(const volatile DmaDescriptor& descriptor) noexcept
    {
        return (descriptor.word3 & descriptor_own) == 0U;
    }

    [[nodiscard]] inline bool transmit_success(const volatile DmaDescriptor& descriptor) noexcept
    {
        const auto status = descriptor.word3;
        return (status & (descriptor_own | descriptor_error)) == 0U;
    }

    [[nodiscard]] constexpr bool inside_dma_region(std::uint32_t address, std::size_t size,
                                                   std::uint32_t base,
                                                   std::size_t region_size) noexcept
    {
        return address >= base && size <= region_size &&
               static_cast<std::size_t>(address - base) <= region_size - size;
    }
}
