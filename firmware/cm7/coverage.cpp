#include "stm32h7xx.h"
extern "C" {
#include <gcov.h>
}
#include <cstddef>
#include <cstdint>

extern "C" {
extern const gcov_info* const __gcov_info_start[];
extern const gcov_info* const __gcov_info_end[];
volatile std::uint32_t g_coverage_request{};
volatile std::uint32_t g_coverage_size{};
volatile std::uint32_t g_coverage_error{};
std::uint8_t g_coverage_bytes[65536]{};
}
namespace {
alignas(std::max_align_t) std::uint8_t allocation[4096]{};
std::size_t allocated{};
void dump(const void* data, unsigned size, void*)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto start = g_coverage_size;
    if (size > sizeof(g_coverage_bytes) - start) { g_coverage_error=1U; return; }
    for (unsigned i=0;i<size;++i) g_coverage_bytes[start+i]=bytes[i];
    g_coverage_size=start+size;
}
void filename(const char* name, void* context) { __gcov_filename_to_gcfn(name,dump,context); }
void* allocate(unsigned size, void*)
{
    constexpr auto alignment=alignof(std::max_align_t);
    const auto start=(allocated+alignment-1U)&~(alignment-1U);
    if (size > sizeof(allocation)-start) { g_coverage_error=2U; return nullptr; }
    allocated=start+size; return allocation+start;
}
}
void coverage_poll()
{
    if (g_coverage_request != 1U) return;
    const auto prior=__get_PRIMASK();
    __disable_irq();
    g_coverage_size=0U;
    g_coverage_error=0U;
    allocated=0U;
    for (auto info=__gcov_info_start;info!=__gcov_info_end;++info)
        __gcov_info_to_gcda(*info,filename,dump,allocate,nullptr);
    g_coverage_request=2U;
    __set_PRIMASK(prior);
}
