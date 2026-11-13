#include "h755/ethernet/cmsis_mdio.hpp"
#include "h755/ethernet/lan8742.hpp"
#include <contracts>
#include <cstdint>
#include <expected>

namespace {
struct Diagnostics {
    std::uint32_t magic;
    std::uint32_t state;
    std::uint32_t heartbeat;
    std::uint32_t hclk_hz;
    std::uint32_t phy_address;
    std::uint32_t phy_id1;
    std::uint32_t phy_id2;
    std::uint32_t mdio_errors;
    std::uint32_t fault;
    std::uint32_t m4_marker;
};
volatile std::uint32_t ticks{};
constexpr std::uint32_t red = 1U << 14;
constexpr std::uint32_t green = 1U;

[[noreturn]] void stop(std::uint32_t reason) noexcept;
}
extern "C" {
volatile Diagnostics g_bringup{};
struct LinkDiagnostics { std::uint32_t state, samples, errors; };
volatile LinkDiagnostics g_link{0xFFFFFFFFU,0U,0U};
extern volatile const std::uint32_t cm4_parked;
void SysTick_Handler() { ticks = ticks + 1U; }
}
namespace {
[[noreturn]] void stop(std::uint32_t reason) noexcept
{
    __disable_irq();
    g_bringup.fault = reason;
    g_bringup.state = 0xFFFFFFFFU;
    RCC->AHB4ENR = RCC->AHB4ENR | RCC_AHB4ENR_GPIOBEN;
    (void)RCC->AHB4ENR;
    GPIOB->MODER = (GPIOB->MODER & ~((3U << 28) | 3U)) | (1U << 28) | 1U;
    GPIOB->BSRR = red | (green << 16);
    __DSB();
    for (;;) __WFI();
}
void alternate11(GPIO_TypeDef& port, unsigned pin) noexcept
{
    contract_assert(pin < 16U);
    const auto shift = pin * 2U;
    port.MODER = (port.MODER & ~(3U << shift)) | (2U << shift);
    port.OTYPER = port.OTYPER & ~(1U << pin);
    port.OSPEEDR = (port.OSPEEDR & ~(3U << shift)) | (3U << shift);
    port.PUPDR = port.PUPDR & ~(3U << shift);
    const auto bank = pin / 8U;
    const auto af_shift = (pin % 8U) * 4U;
    port.AFR[bank] = (port.AFR[bank] & ~(15U << af_shift)) | (11U << af_shift);
}
void configure_pins() noexcept
{
    RCC->AHB4ENR = RCC->AHB4ENR | RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN;
    RCC->APB4ENR = RCC->APB4ENR | RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;
    GPIOB->BSRR = (green | red) << 16;
    GPIOB->MODER = (GPIOB->MODER & ~((3U << 28) | 3U)) | (1U << 28) | 1U;
    alternate11(*GPIOA, 1U);
    alternate11(*GPIOA, 2U);
    alternate11(*GPIOC, 1U);
    SYSCFG->PMCR = (SYSCFG->PMCR & ~SYSCFG_PMCR_EPIS_SEL) | SYSCFG_PMCR_EPIS_SEL_2;
    RCC->AHB1RSTR = RCC->AHB1RSTR | RCC_AHB1RSTR_ETH1MACRST;
    __DSB();
    RCC->AHB1RSTR = RCC->AHB1RSTR & ~RCC_AHB1RSTR_ETH1MACRST;
    RCC->AHB1ENR = RCC->AHB1ENR | RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN;
    (void)RCC->AHB1ENR;
    __DSB();
}
using namespace h755::ethernet;
using Reader = MdioReader<CmsisMdioRegisters>;
std::expected<std::uint16_t, MdioError> read_phy(Reader& reader, PhyAddress phy, std::uint32_t reg) noexcept
{
    const auto address = RegisterAddress::from(reg);
    contract_assert(address.has_value());
    if (auto started = reader.begin(phy, *address, ticks, 20U); !started)
        return std::unexpected{started.error()};
    for (;;) {
        auto result = reader.poll(ticks);
        if (!result) return std::unexpected{result.error()};
        if (result->has_value()) return **result;
    }
}
}
void handle_contract_violation(const std::contracts::contract_violation&) { stop(1U); }
extern "C" {
// GNU ld wraps the pinned runtime entry points without redefining std functions.
[[noreturn]] void __wrap__ZSt21__glibcxx_assert_failPKciS0_S0_(const char*, int, const char*, const char*) noexcept { stop(2U); }
[[noreturn]] void __wrap__ZSt9terminatev() noexcept { stop(3U); }
void _init() {}
[[noreturn]] void HardFault_Handler() { stop(4U); }
[[noreturn]] void MemManage_Handler() { stop(5U); }
[[noreturn]] void BusFault_Handler() { stop(6U); }
[[noreturn]] void UsageFault_Handler() { stop(7U); }
}
int main()
{
    g_bringup.magic = 0x48373535U;
    g_bringup.state = 1U;
    g_bringup.phy_address = 0xFFFFFFFFU;
    SystemCoreClockUpdate();
    g_bringup.hclk_hz = SystemD2Clock;
    if (SystemCoreClock != 64'000'000U || SystemD2Clock != 64'000'000U) stop(8U);
    configure_pins();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) stop(9U);
    const auto started = ticks;
    while (cm4_parked != 0x5041524BU) {
        if (ticks - started >= 100U) stop(10U);
    }
    g_bringup.m4_marker = cm4_parked;
    g_bringup.state = 2U;
    // Delay for the PHY power-on reset; no MAC/DMA transmission is enabled.
    while (ticks - started < 200U) __WFI();
    CmsisMdioRegisters registers{*ETH};
    const auto clock = MdioClock::from_hclk(SystemD2Clock);
    contract_assert(clock.has_value());
    Reader reader{registers, *clock};
    g_bringup.state = 3U;
    for (std::uint32_t address = 0; address < 32U; ++address) {
        const auto phy = PhyAddress::from(address);
        contract_assert(phy.has_value());
        const auto id1 = read_phy(reader, *phy, 2U);
        const auto id2 = read_phy(reader, *phy, 3U);
        if (!id1 || !id2) {
            g_bringup.mdio_errors = g_bringup.mdio_errors + 1U;
            if (!reader.recover_after_idle()) break;
        } else if (is_lan8742a(*id1, *id2)) {
            g_bringup.phy_address = address;
            g_bringup.phy_id1 = *id1;
            g_bringup.phy_id2 = *id2;
            break;
        }
    }
    const bool found = g_bringup.phy_address != 0xFFFFFFFFU;
    g_bringup.state = found ? 4U : 5U;
    GPIOB->BSRR = found ? red << 16 : red;
    const auto phy = PhyAddress::from(found ? g_bringup.phy_address : 0U);
    contract_assert(phy.has_value());
    Lan8742LinkMonitor monitor{reader, *phy};
    auto last_link = ticks;
    bool active = false;
    auto last = ticks;
    bool lit = false;
    for (;;) {
        const auto now = ticks;
        if (found && !active && now - last_link >= 250U) {
            last_link = now;
            active = monitor.begin(now,20U).has_value();
        }
        if (active) {
            const auto link = monitor.poll(now);
            if (!link) {
                active = false;
                g_link.state = 0xFFFFFFFFU;
                g_link.errors = g_link.errors + 1U;
                (void)reader.recover_after_idle();
            } else if (link->has_value()) {
                active = false;
                g_link.state = static_cast<std::uint32_t>(**link);
                g_link.samples = g_link.samples + 1U;
            }
        }
#ifdef H755_TARGET_COVERAGE
        extern void coverage_poll();
        coverage_poll();
#endif
        if (now - last >= 500U) {
            last = now;
            lit = !lit;
            GPIOB->BSRR = lit ? green : green << 16;
            g_bringup.heartbeat = g_bringup.heartbeat + 1U;
        }
        __WFI();
    }
}
