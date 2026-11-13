#include "h755/ethernet/mdio.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace h755::ethernet;

namespace {
int failures = 0;
void check(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

struct FakeRegisters {
    std::uint32_t address{};
    std::uint32_t data{};
    unsigned writes{};
    unsigned reads{};
    unsigned address_reads{};
    std::uint32_t read_address() noexcept { ++address_reads; return address; }
    std::uint32_t read_data() noexcept { ++reads; return data; }
    void write_address(std::uint32_t value) noexcept { address = value; ++writes; }
};

void unit_tests()
{
    static_assert(!std::is_convertible_v<PhyAddress, RegisterAddress>);
    check(PhyAddress::from(0).has_value(), "PHY address zero valid");
    check(PhyAddress::from(31).has_value(), "PHY address 31 valid");
    check(!PhyAddress::from(32), "PHY address 32 rejected");
    check(!PhyAddress::from(0xFFFFFFFFU), "PHY address must not truncate");
    check(!MdioClock::from_hclk(0), "zero clock rejected");
    check(!MdioClock::from_hclk(240'000'001), "unsupported H755 HCLK rejected");
    struct ClockCase { std::uint32_t hclk; std::uint32_t bits; };
    constexpr ClockCase cases[] = {
        {1, 0x200}, {34'999'999, 0x200}, {35'000'000, 0x300},
        {59'999'999, 0x300}, {60'000'000, 0}, {99'999'999, 0},
        {100'000'000, 0x100}, {149'999'999, 0x100},
        {150'000'000, 0x400}, {240'000'000, 0x400}
    };
    for (const auto& item : cases) {
        const auto clock = MdioClock::from_hclk(item.hclk);
        check(clock && clock->bits() == item.bits, "MDIO divider boundary");
    }
    check(is_lan8742a(0x0007, 0xC130), "LAN8742A recognized");
    check(is_lan8742a(0x0007, 0xC13F), "PHY revision does not change model");
    check(!is_lan8742a(0xFFFF, 0xFFFF), "floating bus rejected");
    check(!is_lan8742a(0, 0), "zero bus rejected");
    check(!is_lan8742a(0x0007, 0xC120), "different PHY model rejected");
}

void integration_tests()
{
    const auto clock = MdioClock::from_hclk(200'000'000);
    check(clock.has_value(), "test clock configuration available");
    if (!clock) return;
    const auto phy = *PhyAddress::from(3);
    const auto reg = *RegisterAddress::from(2);
    FakeRegisters io;
    MdioReader reader{io, *clock};
    check(!reader.poll(0), "poll without request rejected");
    check(!reader.begin(phy, reg, 0, 0), "zero timeout rejected");
    check(!reader.begin(phy, reg, 0, 0x80000000U), "ambiguous timeout rejected");
    check(io.writes == 0 && io.address_reads == 0, "invalid requests do not touch hardware");
    check(reader.begin(phy, reg, 100, 10).has_value(), "begin read succeeds");
    check(io.address == 0x0062040DU, "Clause 22 read uses exact PA/RDA/CR/GOC/MB bits");
    check(!reader.begin(phy, reg, 101, 10), "second request cannot overwrite first");
    const auto pending = reader.poll(109);
    check(pending && !pending->has_value(), "busy before deadline remains pending");
    check(io.reads == 0, "busy data never consumed");
    const auto timed_out = reader.poll(110);
    check(!timed_out && timed_out.error() == MdioError::timeout, "busy at deadline times out");
    check(!reader.begin(phy, reg, 111, 10), "timeout latches recovery state");
    check(!reader.recover_after_idle(), "busy hardware cannot be recovered blindly");
    io.address &= ~1U;
    io.data = 0xABCD0007;
    check(reader.recover_after_idle().has_value(), "recovery after hardware idle");
    check(reader.begin(phy, reg, 0xFFFFFFFCU, 8).has_value(), "start before tick rollover");
    const auto wrapped_pending = reader.poll(2);
    check(wrapped_pending && !wrapped_pending->has_value(), "timeout arithmetic crosses rollover");
    io.address &= ~1U;
    const auto value = reader.poll(4);
    check(value && value->has_value() && **value == 7, "completed read returns only 16-bit data");
    check(!reader.poll(5), "completed data consumed once");
    io.address |= 1U;
    const auto hardware_busy = reader.begin(phy, reg, 10, 10);
    check(!hardware_busy && hardware_busy.error() == MdioError::busy, "external hardware busy rejected");
    io.address &= ~1U;
    check(reader.begin(phy, reg, 0xFFFFFFFCU, 8).has_value(), "start second rollover request");
    const auto wrapped_timeout = reader.poll(4);
    check(!wrapped_timeout && wrapped_timeout.error() == MdioError::timeout,
          "busy deadline expires across tick rollover");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "unit") == 0) unit_tests();
    else if (std::strcmp(argv[1], "integration") == 0) integration_tests();
    else return 2;
    return failures == 0 ? 0 : 1;
}
