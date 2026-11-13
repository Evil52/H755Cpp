#pragma once
#include <cstdint>
namespace h755 {
enum class FaultCode : std::uint32_t {
    contract = 1, assertion = 2, termination = 3, hard_fault = 4,
    memory_fault = 5, bus_fault = 6, usage_fault = 7,
    clock = 8, timebase = 9, m4_timeout = 10, invalid_state = 11
};
namespace fault { [[noreturn]] void stop(FaultCode reason) noexcept; }
}
