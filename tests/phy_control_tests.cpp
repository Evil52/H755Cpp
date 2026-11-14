#include "h755/ethernet/phy_control.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using namespace h755::ethernet;
namespace {
void check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Registers {
    struct Command { unsigned reg; bool write; std::uint16_t data; };
    std::array<Command, 2048> commands{};
    std::uint32_t address{}, data{};
    unsigned count{}, reset_reads{}, mode_reads{};
    std::uint16_t bmcr{0x1000}, status{0x24}, resolved{0x1018}, control_override{0xFFFF};
    bool hold{}, stuck_reset{}, stuck_restart{}, floating_reset{}, invalid_control{}, drop_after_mode{};
    bool staged{}, staged_before_busy{true};
    void write_data(std::uint16_t value) { check((address & 1U) == 0, "MDIO data staged only while idle"); data = value; staged = true; }
    void write_address(std::uint32_t value)
    {
        check(count < commands.size(), "test command storage bounded");
        const bool write = (value & 0xCU) == 4U;
        staged_before_busy = staged_before_busy && (!write || staged);
        commands[count++] = {static_cast<unsigned>((value >> 16U) & 31U), write, static_cast<std::uint16_t>(data)};
        address = value; staged = false;
    }
    std::uint32_t read_address()
    {
        if ((address & 1U) == 0 || hold) return address;
        const auto reg = (address >> 16U) & 31U;
        if ((address & 0xCU) == 4U) {
            check(reg == 0, "startup writes only BMCR");
            bmcr = static_cast<std::uint16_t>(data);
            if ((bmcr & 0x8000U) != 0) reset_reads = 0;
        } else {
            if (reg == 0) {
                if ((bmcr & 0x8000U) != 0) {
                    ++reset_reads;
                    if (!stuck_reset && reset_reads > 1) bmcr = 0x1000;
                    data = floating_reset ? 0xFFFFU : bmcr;
                } else {
                    if (!stuck_restart) bmcr = static_cast<std::uint16_t>(bmcr & ~0x200U);
                    data = invalid_control ? control_override : bmcr;
                }
            } else if (reg == 1) data = drop_after_mode && mode_reads > 0 ? 0U : status;
            else if (reg == 31) { data = resolved; ++mode_reads; }
            else check(false, "unexpected startup register");
        }
        address &= ~1U;
        return address;
    }
    std::uint32_t read_data() const { return data; }
};
struct Fixture {
    Registers registers;
    MdioTransport<Registers> bus{registers, *MdioClock::from_hclk(64'000'000)};
    Lan8742Control<decltype(bus)> control{bus, *PhyAddress::from(0)};
    std::uint32_t now{};
    std::expected<std::optional<LinkState>, PhyError> run(unsigned limit = 7000)
    {
        for (unsigned i = 0; i < limit; ++i) {
            ++now;
            auto result = control.poll(now);
            if (!result || result->has_value()) return result;
        }
        check(false, "startup must terminate within bounded test budget");
        return std::unexpected{PhyError::no_request};
    }
};
void expect_error(const std::expected<std::optional<LinkState>, PhyError>& result, PhyError expected, const char* message)
{
    check(!result && result.error() == expected, message);
}
void success(std::uint16_t mode, LinkState expected)
{
    Fixture f; f.registers.resolved = mode;
    check(f.control.state() == PhyState::idle && !f.control.poll(0), "unstarted control is idle");
    check(f.control.begin(0).has_value(), "PHY startup begins");
    check(f.control.state() == PhyState::resetting, "reset phase exposed");
    check(f.registers.count == 1 && f.registers.commands[0].write && f.registers.commands[0].data == 0x8000,
          "reset write sets only reset bit");
    check(!f.control.begin(1), "concurrent startup rejected");
    const auto result = f.run();
    check(result && result->has_value() && **result == expected && f.control.state() == PhyState::ready, "real MDIO resolves expected link mode");
    const auto snapshot = f.control.snapshot();
    check(snapshot.bmcr == 0x1000 && snapshot.bmsr == 0x24 && snapshot.resolved == mode, "accepted register snapshot matches real transaction values");
    check(f.registers.staged_before_busy && f.registers.reset_reads == 2, "reset clears before negotiation write");
    unsigned writes = 0;
    for (unsigned i = 0; i < f.registers.count; ++i) {
        const auto command = f.registers.commands[i];
        if (command.write) {
            check(command.reg == 0 && command.data == (writes == 0 ? 0x8000 : 0x1200), "only documented BMCR commands emitted");
            ++writes;
        }
    }
    check(writes == 2 && !f.control.poll(++f.now), "commands and completion occur once");
    check(f.control.begin(f.now).has_value(), "explicit new startup allowed after completion");
}
void validation()
{
    Fixture f;
    for (auto timing : {PhyTiming{0,600,5000,10}, PhyTiming{20,0,5000,10}, PhyTiming{20,600,0,10},
                        PhyTiming{20,600,5000,0}, PhyTiming{0x80000000U,600,5000,10},
                        PhyTiming{20,0x80000000U,5000,10}, PhyTiming{20,600,0x80000000U,10},
                        PhyTiming{20,600,5000,0x80000000U}, PhyTiming{20,5,5000,10}}) {
        const auto result = f.control.begin(0,timing);
        check(!result && result.error() == PhyError::invalid_timing, "invalid timing rejected");
    }
    check(f.registers.count == 0, "invalid timing touches no registers");
}
void failures()
{
    Fixture busy; busy.registers.address = 1; busy.registers.hold = true;
    check(!busy.control.begin(0), "existing MDIO owner not overwritten");
    Fixture stuck; stuck.registers.stuck_reset = true;
    check(stuck.control.begin(0).has_value(), "stuck-reset probe begins");
    const auto reset = stuck.run();
    check(!reset && reset.error() == PhyError::reset_timeout && stuck.now == 600, "reset has a whole-phase deadline");
    check(stuck.registers.count < 70 && stuck.control.state() == PhyState::failed, "reset polling is rate bounded");
    Fixture floating; floating.registers.floating_reset = true; floating.registers.stuck_reset = true;
    check(floating.control.begin(0).has_value(), "floating-reset probe begins");
    expect_error(floating.run(), PhyError::reset_timeout, "floating reset reads cannot falsely complete reset");
    Fixture no_cable; no_cable.registers.status = 0;
    check(no_cable.control.begin(0).has_value(), "no-cable probe begins");
    const auto no_link = no_cable.run();
    check(!no_link && no_link.error() == PhyError::negotiation_timeout, "missing cable has bounded negotiation deadline");
    const auto prior = no_cable.registers.count; check(!no_cable.control.poll(++no_cable.now) && no_cable.registers.count == prior, "timeout does not silently restart PHY");
    check(no_cable.bus.begin(*PhyAddress::from(0),*RegisterAddress::from(1),no_cable.now,20).has_value(), "negotiation timeout leaves bus idle for link observation");
    Fixture restart; restart.registers.stuck_restart = true;
    check(restart.control.begin(0).has_value(), "restart-stuck probe begins");
    expect_error(restart.run(), PhyError::configuration_timeout, "restart self-clear is bounded");
    Fixture late;
    check(late.control.begin(0).has_value(), "late configuration probe begins");
    while (late.registers.count < 4) { ++late.now; const auto r = late.control.poll(late.now); check(r.has_value(), "configuration progresses"); }
    late.now += 5000;
    const auto expired = late.control.poll(late.now);
    check(!expired && expired.error() == PhyError::configuration_timeout, "late write completion cannot claim verified negotiation configuration");
    Fixture transport; transport.registers.hold = true;
    check(transport.control.begin(0).has_value(), "transport-timeout probe begins");
    check(transport.run().error() == PhyError::transport && transport.now == 20, "MDIO timeout not mistaken for a cancellable negotiation timeout");
    check(!transport.bus.recover_after_idle(), "busy hardware remains faulted");
    transport.registers.hold = false; transport.registers.read_address();
    check(transport.bus.recover_after_idle().has_value(), "explicit bus recovery after idle");
    Fixture invalid; invalid.registers.invalid_control = true;
    check(invalid.control.begin(0).has_value(), "invalid-control probe begins");
    expect_error(invalid.run(), PhyError::invalid_status, "floating BMCR rejected after reset");
    for (const std::uint16_t value : std::array<std::uint16_t,4>{0,0x5000,0x1800,0x1400}) {
        Fixture invalid_bits; invalid_bits.registers.invalid_control = true; invalid_bits.registers.control_override = value;
        check(invalid_bits.control.begin(0).has_value(), "BMCR policy probe begins");
        expect_error(invalid_bits.run(), PhyError::invalid_status, "disabled AN, loopback, power-down or isolate rejected");
    }
    Fixture incomplete; incomplete.registers.status = 4;
    check(incomplete.control.begin(0).has_value(), "BMSR incomplete probe begins");
    expect_error(incomplete.run(), PhyError::negotiation_timeout, "link bit alone cannot complete negotiation");
    Fixture read_stuck; check(read_stuck.control.begin(0).has_value(), "read timeout probe begins");
    while (read_stuck.registers.count < 2) { ++read_stuck.now; check(read_stuck.control.poll(read_stuck.now).has_value(), "first read starts"); }
    read_stuck.registers.hold = true;
    expect_error(read_stuck.run(), PhyError::transport, "MDIO read timeout remains explicit and latched");
    Fixture remote; remote.registers.status = 0x34;
    check(remote.control.begin(0).has_value(), "remote-fault probe begins");
    expect_error(remote.run(), PhyError::invalid_status, "current remote fault cannot publish ready");
    Fixture reserved; reserved.registers.resolved = 0x1000;
    check(reserved.control.begin(0).has_value(), "reserved-mode probe begins");
    expect_error(reserved.run(), PhyError::invalid_status, "reserved resolution rejected");
    Fixture drop; drop.registers.drop_after_mode = true;
    check(drop.control.begin(0).has_value(), "link-drop probe begins");
    expect_error(drop.run(), PhyError::negotiation_timeout, "final live BMSR rejects link lost during mode read");
    Fixture pending; pending.registers.resolved = 0;
    check(pending.control.begin(0).has_value(), "incomplete-resolution probe begins");
    expect_error(pending.run(), PhyError::negotiation_timeout, "resolution must finish before ready");
    Fixture wrap; wrap.now = 0xFFFFFFD0U;
    check(wrap.control.begin(wrap.now).has_value(), "wrap probe begins");
    const auto done = wrap.run(); check(done && done->has_value() && wrap.now < 1000U, "startup spans counter rollover");
}
}
int main()
{
    static_assert(!std::is_copy_constructible_v<Lan8742Control<MdioTransport<Registers>>>);
    success(0x1004,LinkState::mb10_half); success(0x1014,LinkState::mb10_full);
    success(0x1008,LinkState::mb100_half); success(0x1018,LinkState::mb100_full);
    validation(); failures();
}
