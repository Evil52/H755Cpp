#include "h755/ethernet/frame_ring.hpp"

#include <cstdio>
#include <cstdlib>

template <class T> static void check(T&& result)
{
    if (!static_cast<bool>(result))
    {
        std::fputs("frame ring test failed\n", stderr);
        std::abort();
    }
}

using h755::ethernet::RingError;
using h755::ethernet::RxOwner;
using h755::ethernet::RxRing;
using h755::ethernet::TxOwner;
using h755::ethernet::TxRing;

static_assert(sizeof(TxRing<4U, 1536U>) <= 24U);
static_assert(sizeof(RxRing<4U, 1536U>) <= 16U);

int main()
{
    TxRing<2U, 1536U> tx;
    const auto first = tx.acquire();
    check(first && *first == 0U);
    const auto second = tx.acquire();
    check(second && *second == 1U);
    check(tx.acquire().error() == RingError::full);
    check(tx.submit(*first, 0U).error() == RingError::invalid_length);
    check(tx.submit(*first, 1537U).error() == RingError::invalid_length);
    check(tx.owner(*first) == TxOwner::producer);
    check(tx.submit(*first, 60U));
    check(tx.owner(*first) == TxOwner::dma && tx.length(*first) == 60U);
    check(tx.submit(*first, 60U).error() == RingError::invalid_state);
    check(tx.cancel(*first).error() == RingError::invalid_state);
    check(tx.reclaim(*first));
    check(tx.owner(*first) == TxOwner::free && tx.length(*first) == 0U);
    check(tx.reclaim(*first).error() == RingError::invalid_state);
    check(tx.cancel(*second));
    check(tx.acquire() == 0U);
    check(tx.acquire() == 1U);
    check(tx.submit(2U, 60U).error() == RingError::invalid_slot);
    check(tx.reclaim(2U).error() == RingError::invalid_slot);
    check(tx.cancel(2U).error() == RingError::invalid_slot);
    check(!tx.owner(2U) && !tx.length(2U));

    RxRing<2U, 1536U> rx;
    check(!rx.ready());
    check(rx.complete(2U, 60U).error() == RingError::invalid_slot);
    check(rx.complete(0U, 0U).error() == RingError::invalid_length);
    check(rx.complete(0U, 1537U).error() == RingError::invalid_length);
    check(rx.complete(0U, 64U));
    check(rx.complete(1U, 1536U));
    check(rx.ready() == 0U);
    check(rx.owner(0U) == RxOwner::consumer && rx.length(0U) == 64U);
    check(rx.complete(0U, 64U).error() == RingError::invalid_state);
    check(rx.release(0U));
    check(rx.ready() == 1U && rx.owner(0U) == RxOwner::dma && rx.length(0U) == 0U);
    check(rx.release(1U));
    check(!rx.ready());
    check(rx.release(0U).error() == RingError::invalid_state);
    check(rx.release(2U).error() == RingError::invalid_slot);
    check(!rx.owner(2U) && !rx.length(2U));
}
