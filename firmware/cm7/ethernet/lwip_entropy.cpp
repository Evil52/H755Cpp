#include "h755/fault.hpp"
#include "stm32h7xx.h"
#include <cstdint>

extern "C" void h755_lwip_assert(const char*)
{
    h755::fault::stop(h755::FaultCode::ethernet);
}

extern "C" std::uint32_t h755_lwip_random()
{
    RCC->CR = RCC->CR | RCC_CR_HSI48ON;
    for (std::uint32_t attempt = 0U; attempt < 100'000U; ++attempt)
    {
        if ((RCC->CR & RCC_CR_HSI48RDY) != 0U)
        {
            break;
        }
        if (attempt == 99'999U)
        {
            h755::fault::stop(h755::FaultCode::ethernet);
        }
    }
    RCC->D2CCIP2R = RCC->D2CCIP2R & ~RCC_D2CCIP2R_RNGSEL;
    RCC->AHB2ENR = RCC->AHB2ENR | RCC_AHB2ENR_RNGEN;
    (void)RCC->AHB2ENR;
    RNG->CR = RNG->CR | RNG_CR_RNGEN;
    for (std::uint32_t attempt = 0U; attempt < 100'000U; ++attempt)
    {
        const auto status = RNG->SR;
        if ((status & (RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) != 0U)
        {
            h755::fault::stop(h755::FaultCode::ethernet);
        }
        if ((status & RNG_SR_DRDY) != 0U)
        {
            return RNG->DR;
        }
    }
    h755::fault::stop(h755::FaultCode::ethernet);
}
