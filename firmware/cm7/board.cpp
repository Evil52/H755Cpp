#include "board.hpp"
#include "h755/board/gpio.hpp"
#include "h755/ethernet/cmsis_mdio.hpp"
#include "h755/platform/processor.hpp"
#include "stm32h7xx.h"
extern "C" volatile const std::uint32_t cm4_parked;
namespace h755 {
void Board::configure() noexcept
{
    RCC->AHB4ENR = RCC->AHB4ENR | RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN;
    RCC->APB4ENR = RCC->APB4ENR | RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;
    gpio::configure_leds(*GPIOB);
    gpio::alternate<1, 11>(*GPIOA);
    gpio::alternate<2, 11>(*GPIOA);
    gpio::alternate<1, 11>(*GPIOC);
    SYSCFG->PMCR = (SYSCFG->PMCR & ~SYSCFG_PMCR_EPIS_SEL) | SYSCFG_PMCR_EPIS_SEL_2;
    RCC->AHB1RSTR = RCC->AHB1RSTR | RCC_AHB1RSTR_ETH1MACRST;
    processor::barrier();
    RCC->AHB1RSTR = RCC->AHB1RSTR & ~RCC_AHB1RSTR_ETH1MACRST;
    RCC->AHB1ENR = RCC->AHB1ENR | RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN;
    (void)RCC->AHB1ENR;
    processor::barrier();
}
ethernet::CmsisMdioRegisters Board::mdio_registers() noexcept { return ethernet::CmsisMdioRegisters{*ETH}; }
std::uint32_t Board::hclk_hz() const noexcept
{
    SystemCoreClockUpdate();
    return SystemCoreClock == 64'000'000U && SystemD2Clock == 64'000'000U ? SystemD2Clock : 0U;
}
std::uint32_t Board::m4_marker() const noexcept { return cm4_parked; }
void Board::green(bool enabled) noexcept { gpio::set_green(*GPIOB, enabled); }
void Board::red(bool enabled) noexcept { gpio::set_red(*GPIOB, enabled); }
void Board::idle() noexcept { processor::idle(); }
namespace board {
void emergency_led() noexcept
{
    // Faults can precede Board/application initialization.
    RCC->AHB4ENR = RCC->AHB4ENR | RCC_AHB4ENR_GPIOBEN;
    (void)RCC->AHB4ENR;
    gpio::configure_leds(*GPIOB);
    gpio::set_red(*GPIOB, true);
}
}
}
