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
    unsigned data_writes{};
    bool data_precedes_command{};
    void write_data(std::uint16_t value) noexcept { data = value; ++data_writes; }
    std::uint32_t read_address() noexcept { ++address_reads; return address; }
    std::uint32_t read_data() noexcept { ++reads; return data; }
    void write_address(std::uint32_t value) noexcept { data_precedes_command = data_writes > 0 && (address & 1U) == 0U; address = value; ++writes; }
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

void write_tests()
{
    FakeRegisters io;
    MdioTransport bus{io, *MdioClock::from_hclk(200'000'000)};
    const auto phy = *PhyAddress::from(3);
    const auto reg = *RegisterAddress::from(0);
    check(!bus.poll_write(0), "write poll without operation rejected");
    check(!bus.begin_write(phy, reg, 0x8000, 0, 0), "zero write timeout rejected");
    check(!bus.begin_write(phy, reg, 0x8000, 0, 0x80000000U), "ambiguous write timeout rejected");
    check(io.data_writes == 0 && io.writes == 0 && io.address_reads == 0, "invalid write has no MMIO effects");
    if (!bus.begin_write(phy, reg, 0x8000, 100, 10)) { check(false, "write starts"); return; }
    check(io.address == 0x00600405U && io.data == 0x8000 && io.data_precedes_command,
          "Clause22 write uses exact command and stages data before busy");
    check(!bus.begin(phy, reg, 101, 10) && !bus.begin_write(phy, reg, 1, 101, 10), "read/write share one outstanding owner");
    check(io.data_writes == 1, "rejected write preserves data register");
    const auto wrong = bus.poll(102);
    check(!wrong && wrong.error() == MdioError::wrong_operation, "read poll cannot consume write completion");
    auto pending = bus.poll_write(109);
    check(pending && !*pending && io.reads == 0, "pending write never reads data as acknowledgement");
    io.address &= ~1U;
    check(!bus.begin(phy, reg, 109, 10), "hardware completion still belongs to unconsumed write");
    const auto done = bus.poll_write(120);
    check(done && *done && io.reads == 0, "completed write wins even on late poll; no readback fabricated");
    check(!bus.poll_write(121), "write completion consumed once");
    check(bus.begin(phy, reg, 130, 10).has_value(), "read can follow consumed write");
    const auto wrong_write = bus.poll_write(131);
    check(!wrong_write && wrong_write.error() == MdioError::wrong_operation, "write poll cannot consume read");
    check(!bus.begin_write(phy, reg, 5, 131, 10) && io.data_writes == 1, "pending read protects write-data register");
    io.address &= ~1U; io.data = 7;
    const auto read = bus.poll(132); check(read && read->has_value() && **read == 7, "read preserved after wrong poll");
    io.address = 1;
    check(!bus.begin_write(phy, reg, 5, 140, 10) && io.data_writes == 1, "hardware busy blocks data staging");
    io.address = 0;
    check(bus.begin_write(*PhyAddress::from(31), *RegisterAddress::from(31), 0xFFFF, 0xFFFFFFFCU, 8).has_value(), "write range and rollover probe starts");
    check(io.address == 0x03FF0405U && io.data == 0xFFFF, "maximum address and data preserved without truncation");
    const auto wrap = bus.poll_write(2); check(wrap && !*wrap, "write remains pending across rollover");
    const auto timeout = bus.poll_write(4);
    check(!timeout && timeout.error() == MdioError::timeout, "write expires exactly at rollover deadline");
    check(!bus.begin(phy, reg, 5, 10) && !bus.begin_write(phy, reg, 0, 5, 10), "write timeout faults both operations");
    check(!bus.poll_write(5) && !bus.recover_after_idle(), "fault cannot be silently acknowledged or cancelled");
    io.address = 0;
    check(bus.recover_after_idle().has_value(), "write fault recovers only after hardware idle");
    check(bus.begin_write(phy, reg, 0, 20, 10).has_value(), "zero write data is valid after recovery");
    io.address = 0; check(bus.poll_write(21).value_or(false), "recovered write completes");
}

void integration_tests()
{
    const auto clock = MdioClock::from_hclk(200'000'000);
    check(clock.has_value(), "test clock configuration available");
    if (!clock) return;
    const auto phy = *PhyAddress::from(3);
    const auto reg = *RegisterAddress::from(2);
    FakeRegisters io;
    MdioTransport reader{io, *clock};
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
    else if (std::strcmp(argv[1], "integration") == 0) { integration_tests(); write_tests(); }
    else return 2;
    return failures == 0 ? 0 : 1;
}
