#include "h755/fault.hpp"
#include "board.hpp"
#include "h755/diagnostics.hpp"
#include "h755/platform/processor.hpp"
#include <contracts>
namespace h755::fault {
[[noreturn]] void stop(FaultCode reason) noexcept
{
    processor::disable_interrupts();
    record_fault(static_cast<std::uint32_t>(reason));
    board::emergency_led();
    processor::barrier();
    for (;;) processor::idle();
}
}
void handle_contract_violation(const std::contracts::contract_violation&)
{
    h755::fault::stop(h755::FaultCode::contract);
}
extern "C" {
// These names are the pinned GCC runtime entry points wrapped by GNU ld.
[[noreturn]] void __wrap__ZSt21__glibcxx_assert_failPKciS0_S0_(const char*, int, const char*, const char*) noexcept
{ h755::fault::stop(h755::FaultCode::assertion); }
[[noreturn]] void __wrap__ZSt9terminatev() noexcept { h755::fault::stop(h755::FaultCode::termination); }
void _init() {}
[[noreturn]] void HardFault_Handler() { h755::fault::stop(h755::FaultCode::hard_fault); }
[[noreturn]] void MemManage_Handler() { h755::fault::stop(h755::FaultCode::memory_fault); }
[[noreturn]] void BusFault_Handler() { h755::fault::stop(h755::FaultCode::bus_fault); }
[[noreturn]] void UsageFault_Handler() { h755::fault::stop(h755::FaultCode::usage_fault); }
}
