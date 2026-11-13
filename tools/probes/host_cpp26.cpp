#include <array>
#include <bit>
#include <cstdint>
#include <span>

static_assert(__cplusplus > 202302L);
static_assert(std::popcount(std::uint32_t{0xA5}) == 4);

int main()
{
    std::array<std::uint32_t, 3> values{10, 20, 30};
    std::span<std::uint32_t> view{values};
    view[1] = 40;
    return values[1] == 40 && view.size() == 3 ? 0 : 1;
}
