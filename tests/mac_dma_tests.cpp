#include "h755/ethernet/mac_dma.hpp"

#include <array>
#include <cassert>

using namespace h755::ethernet;

int main()
{
    static_assert(sizeof(DmaDescriptor) == 16U);
    static_assert(alignof(DmaDescriptor) == 4U);

    DmaDescriptor rx = rx_descriptor(0x30001000U);
    assert(rx.word0 == 0x30001000U);
    assert(rx.word1 == 0U && rx.word2 == 0U);
    assert(rx.word3 == (descriptor_own | rx_buffer1_valid));

    rx.word3 = descriptor_first | descriptor_last | 64U;
    assert(received_length(rx, 1536U) == 64U);
    rx.word3 = descriptor_first | 64U;
    assert(!received_length(rx, 1536U));
    rx.word3 = descriptor_first | descriptor_last | descriptor_error | 64U;
    assert(!received_length(rx, 1536U));
    rx.word3 = descriptor_context | descriptor_first | descriptor_last | 64U;
    assert(!received_length(rx, 1536U));
    rx.word3 = descriptor_first | descriptor_last | 1537U;
    assert(!received_length(rx, 1536U));
    rx.word3 = descriptor_own | descriptor_first | descriptor_last | 64U;
    assert(!received_length(rx, 1536U));

    assert(!tx_descriptor(0x30002000U, 0U, 1536U));
    assert(!tx_descriptor(0x30002000U, 1537U, 1536U));
    DmaDescriptor tx = *tx_descriptor(0x30002000U, 60U, 1536U);
    assert(tx.word0 == 0x30002000U && tx.word1 == 0U);
    assert(tx.word2 == 60U);
    assert(tx.word3 == (descriptor_own | descriptor_first | descriptor_last | 60U));
    assert(!transmit_complete(tx));
    assert(!transmit_success(tx));
    tx.word3 &= ~descriptor_own;
    assert(transmit_complete(tx));
    assert(transmit_success(tx));
    tx.word3 |= descriptor_error;
    assert(!transmit_success(tx));

    assert(inside_dma_region(0x30000000U, 12416U, 0x30000000U, 128U * 1024U));
    assert(inside_dma_region(0x3001CF80U, 12416U, 0x30000000U, 128U * 1024U));
    assert(!inside_dma_region(0x3001CF81U, 12416U, 0x30000000U, 128U * 1024U));
    assert(!inside_dma_region(0x24000000U, 12416U, 0x30000000U, 128U * 1024U));
}
