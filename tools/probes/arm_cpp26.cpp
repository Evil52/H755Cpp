#include <array>
#include <contracts>
#include <cstdint>
#include <span>

#if !defined(__cpp_contracts) || __cpp_contracts < 202502L
#error Native C++26 contracts are required for this probe
#endif

#if !defined(__cpp_lib_contracts) || __cpp_lib_contracts < 202502L
#error The matching C++26 contracts library header is required
#endif

#ifndef _GLIBCXX_ASSERTIONS
#error libstdc++ assertions must remain enabled
#endif

static_assert(__cplusplus > 202302L);
static_assert(sizeof(void*) == 4);

int positive_value(int value)
    pre(value > 0)
    post(result: result > 0)
{
    contract_assert(value > 0);
    return value;
}

int array_value(std::uint32_t index)
{
    std::array<int, 2> values{1, 2};
    return positive_value(std::span{values}[index]);
}
