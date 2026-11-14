#include "h755/platform/processor.hpp"
#include "stm32h7xx.h"
#include <contracts>
namespace h755::processor
{
    bool start_tick(std::uint32_t cycles) noexcept
    {
        contract_assert(cycles > 0U && cycles <= 0x1000000U);
        return SysTick_Config(cycles) == 0U;
    }
    void stop_tick() noexcept
    {
        SysTick->CTRL = 0U;
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        __DSB();
    }
    void disable_interrupts() noexcept
    {
        __disable_irq();
    }
    void barrier() noexcept
    {
        __DSB();
    }
    void idle() noexcept
    {
        __WFI();
    }
}
