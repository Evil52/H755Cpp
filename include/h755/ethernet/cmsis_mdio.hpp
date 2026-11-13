#pragma once

#include "h755/ethernet/mdio.hpp"
#include "stm32h7xx.h"

namespace h755::ethernet {

class CmsisMdioRegisters {
public:
    explicit CmsisMdioRegisters(ETH_TypeDef& peripheral) noexcept : peripheral_{peripheral} {}
    CmsisMdioRegisters(const CmsisMdioRegisters&) = delete;
    CmsisMdioRegisters& operator=(const CmsisMdioRegisters&) = delete;
    std::uint32_t read_address() const noexcept { return peripheral_.MACMDIOAR; }
    std::uint32_t read_data() const noexcept { return peripheral_.MACMDIODR; }
    void write_address(std::uint32_t value) noexcept { peripheral_.MACMDIOAR = value; }
private:
    ETH_TypeDef& peripheral_;
};

static_assert(ETH_MACMDIOAR_PA_Pos == 21U);
static_assert(ETH_MACMDIOAR_RDA_Pos == 16U);
static_assert(ETH_MACMDIOAR_MOC_RD == 0xCU);
static_assert(ETH_MACMDIOAR_MB == 1U);
static_assert(ETH_MACMDIOAR_CR_DIV16 == 0x200U);
static_assert(ETH_MACMDIOAR_CR_DIV26 == 0x300U);
static_assert(ETH_MACMDIOAR_CR_DIV42 == 0U);
static_assert(ETH_MACMDIOAR_CR_DIV62 == 0x100U);
static_assert(ETH_MACMDIOAR_CR_DIV102 == 0x400U);

}
