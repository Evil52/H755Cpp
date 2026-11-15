#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>

namespace h755::ethernet
{
enum class RingError : std::uint8_t
{
    full,
    invalid_slot,
    invalid_state,
    invalid_length
};

enum class TxOwner : std::uint8_t
{
    free,
    producer,
    dma
};

template <std::size_t Capacity, std::size_t BufferSize>
class TxRing final
{
    static_assert(Capacity > 0U && Capacity <= std::numeric_limits<std::uint16_t>::max());
    static_assert(BufferSize > 0U && BufferSize <= std::numeric_limits<std::uint16_t>::max());

public:
    std::expected<std::size_t, RingError> acquire() noexcept
    {
        auto cursor = next_;
        for (std::size_t scanned = 0U; scanned < Capacity; ++scanned)
        {
            if (slots_[cursor].owner == TxOwner::free)
            {
                slots_[cursor].owner = TxOwner::producer;
                next_ = advance(cursor);
                return cursor;
            }
            cursor = advance(cursor);
        }
        return std::unexpected{RingError::full};
    }

    std::expected<void, RingError> submit(std::size_t index, std::size_t length) noexcept
    {
        if (index >= Capacity)
        {
            return std::unexpected{RingError::invalid_slot};
        }
        auto& slot = slots_[index];
        if (slot.owner != TxOwner::producer)
        {
            return std::unexpected{RingError::invalid_state};
        }
        if (length == 0U || length > BufferSize)
        {
            return std::unexpected{RingError::invalid_length};
        }
        slot.length = static_cast<std::uint16_t>(length);
        slot.owner = TxOwner::dma;
        return {};
    }

    std::expected<void, RingError> reclaim(std::size_t index) noexcept
    {
        return release(index, TxOwner::dma);
    }

    std::expected<void, RingError> cancel(std::size_t index) noexcept
    {
        return release(index, TxOwner::producer);
    }

    [[nodiscard]] std::optional<TxOwner> owner(std::size_t index) const noexcept
    {
        return index < Capacity ? std::optional{slots_[index].owner} : std::nullopt;
    }
    [[nodiscard]] std::optional<std::size_t> length(std::size_t index) const noexcept
    {
        return index < Capacity ? std::optional{slots_[index].length} : std::nullopt;
    }

private:
    static constexpr std::uint16_t advance(std::uint16_t index) noexcept
    {
        return static_cast<std::uint16_t>(index + 1U == Capacity ? 0U : index + 1U);
    }

    std::expected<void, RingError> release(std::size_t index, TxOwner expected) noexcept
    {
        if (index >= Capacity)
        {
            return std::unexpected{RingError::invalid_slot};
        }
        auto& slot = slots_[index];
        if (slot.owner != expected)
        {
            return std::unexpected{RingError::invalid_state};
        }
        slot = {};
        return {};
    }

    struct Slot
    {
        TxOwner owner{TxOwner::free};
        std::uint16_t length{};
    };
    std::array<Slot, Capacity> slots_{};
    std::uint16_t next_{};
};

enum class RxOwner : std::uint8_t
{
    dma,
    consumer
};

template <std::size_t Capacity, std::size_t BufferSize>
class RxRing final
{
    static_assert(Capacity > 0U && Capacity <= std::numeric_limits<std::uint16_t>::max());
    static_assert(BufferSize > 0U && BufferSize <= std::numeric_limits<std::uint16_t>::max());

public:
    std::expected<void, RingError> complete(std::size_t index, std::size_t length) noexcept
    {
        if (index >= Capacity)
        {
            return std::unexpected{RingError::invalid_slot};
        }
        auto& slot = slots_[index];
        if (slot.owner != RxOwner::dma)
        {
            return std::unexpected{RingError::invalid_state};
        }
        if (length == 0U || length > BufferSize)
        {
            return std::unexpected{RingError::invalid_length};
        }
        slot.length = static_cast<std::uint16_t>(length);
        slot.owner = RxOwner::consumer;
        return {};
    }

    std::optional<std::size_t> ready() const noexcept
    {
        for (std::size_t index = 0U; index < Capacity; ++index)
        {
            if (slots_[index].owner == RxOwner::consumer)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    std::expected<void, RingError> release(std::size_t index) noexcept
    {
        if (index >= Capacity)
        {
            return std::unexpected{RingError::invalid_slot};
        }
        auto& slot = slots_[index];
        if (slot.owner != RxOwner::consumer)
        {
            return std::unexpected{RingError::invalid_state};
        }
        slot = {};
        return {};
    }

    [[nodiscard]] std::optional<RxOwner> owner(std::size_t index) const noexcept
    {
        return index < Capacity ? std::optional{slots_[index].owner} : std::nullopt;
    }
    [[nodiscard]] std::optional<std::size_t> length(std::size_t index) const noexcept
    {
        return index < Capacity ? std::optional{slots_[index].length} : std::nullopt;
    }

private:
    struct Slot
    {
        RxOwner owner{RxOwner::dma};
        std::uint16_t length{};
    };
    std::array<Slot, Capacity> slots_{};
};
}
