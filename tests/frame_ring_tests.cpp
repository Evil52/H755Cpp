#include "h755/ethernet/frame_ring.hpp"

#include <cassert>

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
    assert(first && *first == 0U);
    const auto second = tx.acquire();
    assert(second && *second == 1U);
    assert(tx.acquire().error() == RingError::full);
    assert(tx.submit(*first, 0U).error() == RingError::invalid_length);
    assert(tx.submit(*first, 1537U).error() == RingError::invalid_length);
    assert(tx.owner(*first) == TxOwner::producer);
    assert(tx.submit(*first, 60U));
    assert(tx.owner(*first) == TxOwner::dma && tx.length(*first) == 60U);
    assert(tx.submit(*first, 60U).error() == RingError::invalid_state);
    assert(tx.cancel(*first).error() == RingError::invalid_state);
    assert(tx.reclaim(*first));
    assert(tx.owner(*first) == TxOwner::free && tx.length(*first) == 0U);
    assert(tx.reclaim(*first).error() == RingError::invalid_state);
    assert(tx.cancel(*second));
    assert(tx.acquire() == 0U);
    assert(tx.acquire() == 1U);
    assert(tx.submit(2U, 60U).error() == RingError::invalid_slot);
    assert(tx.reclaim(2U).error() == RingError::invalid_slot);
    assert(tx.cancel(2U).error() == RingError::invalid_slot);
    assert(!tx.owner(2U) && !tx.length(2U));

    RxRing<2U, 1536U> rx;
    assert(!rx.ready());
    assert(rx.complete(2U, 60U).error() == RingError::invalid_slot);
    assert(rx.complete(0U, 0U).error() == RingError::invalid_length);
    assert(rx.complete(0U, 1537U).error() == RingError::invalid_length);
    assert(rx.complete(0U, 64U));
    assert(rx.complete(1U, 1536U));
    assert(rx.ready() == 0U);
    assert(rx.owner(0U) == RxOwner::consumer && rx.length(0U) == 64U);
    assert(rx.complete(0U, 64U).error() == RingError::invalid_state);
    assert(rx.release(0U));
    assert(rx.ready() == 1U && rx.owner(0U) == RxOwner::dma && rx.length(0U) == 0U);
    assert(rx.release(1U));
    assert(!rx.ready());
    assert(rx.release(0U).error() == RingError::invalid_state);
    assert(rx.release(2U).error() == RingError::invalid_slot);
    assert(!rx.owner(2U) && !rx.length(2U));
}
