#include "runtime.hpp"
#include "h755/board/gpio.hpp"
#include "stm32h7xx.h"
namespace h755::usb
{
    void Platform::enable_clock()
    {
        RCC->CR = RCC->CR | RCC_CR_HSI48ON;
    }
    bool Platform::clock_ready() const
    {
        return (RCC->CR & RCC_CR_HSI48RDY) != 0U;
    }
    void Platform::configure()
    {
        RCC->AHB4ENR = RCC->AHB4ENR | RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIODEN;
        (void)RCC->AHB4ENR;
        // Device mode must never source VBUS through the board's host power switch.
        GPIOD->BSRR = 1U << 26U;
        GPIOD->MODER = (GPIOD->MODER & ~(3U << 20U)) | (1U << 20U);
        GPIOD->OTYPER = GPIOD->OTYPER & ~(1U << 10U);
        gpio::alternate<11, 10>(*GPIOA);
        gpio::alternate<12, 10>(*GPIOA);
        GPIOA->MODER = GPIOA->MODER & ~(3U << 18U);
        GPIOA->PUPDR = GPIOA->PUPDR & ~(3U << 18U);
        PWR->CR3 = PWR->CR3 | PWR_CR3_USB33DEN;
        RCC->D2CCIP2R = (RCC->D2CCIP2R & ~RCC_D2CCIP2R_USBSEL) | RCC_D2CCIP2R_USBSEL;
        RCC->APB1HENR = RCC->APB1HENR | RCC_APB1HENR_CRSEN;
        (void)RCC->APB1HENR;
        CRS->CR = 32U << CRS_CR_TRIM_Pos;
        CRS->CFGR = CRS_CFGR_SYNCSRC | ((0x22U << CRS_CFGR_FELIM_Pos)) | 47'999U;
        CRS->CR = CRS->CR | CRS_CR_AUTOTRIMEN | CRS_CR_CEN;
        RCC->AHB1ENR = RCC->AHB1ENR | RCC_AHB1ENR_USB2OTGFSEN;
        (void)RCC->AHB1ENR;
        RCC->AHB1RSTR = RCC->AHB1RSTR | RCC_AHB1RSTR_USB2OTGFSRST;
        __DSB();
        RCC->AHB1RSTR = RCC->AHB1RSTR & ~RCC_AHB1RSTR_USB2OTGFSRST;
        (void)RCC->AHB1RSTR;
        NVIC_ClearPendingIRQ(OTG_FS_IRQn);
        NVIC_SetPriority(OTG_FS_IRQn, 15U);
    }
    void Platform::disconnect()
    {
        NVIC_DisableIRQ(OTG_FS_IRQn);
        USB2_OTG_FS->GINTMSK = 0;
        static_assert(
            (USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE) % alignof(USB_OTG_DeviceTypeDef) == 0);
        constexpr auto device_address = USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE;
        // CMSIS device block at FS base+0x800; typed, aligned MMIO boundary reviewed against
        // STM32H755 definitions.

        // nosemgrep: security.semgrep.h755-no-reinterpret-cast
        auto* device = reinterpret_cast<USB_OTG_DeviceTypeDef*>(device_address);
        device->DCTL = device->DCTL | USB_OTG_DCTL_SDIS;
    }
}
