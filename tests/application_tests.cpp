#include "h755/application/controller.hpp"
#include "h755/board/gpio.hpp"
#include "h755/timebase.hpp"
#include "h755/platform/processor.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Clock {
    std::uint32_t tick{};
    bool available{true};
    bool start(std::uint32_t) { return available; }
    std::uint32_t now() const { return tick; }
};
struct Board {
    Clock& clock;
    std::uint32_t hz{64'000'000};
    bool parked{true}, configured{}, green_led{}, red_led{};
    void configure() { configured = true; }
    std::uint32_t hclk_hz() const { return hz; }
    std::uint32_t m4_marker() const { return parked ? 0x5041524BU : 0U; }
    void idle() { clock.tick += 1U; }
    void green(bool value) { green_led = value; }
    void red(bool value) { red_led = value; }
};
struct Registers {
    std::uint32_t address{}, data{}, writes{};
    unsigned phy{3};
    bool hold{}, hold_after_write{};
    std::uint32_t read_address()
    {
        if ((address & 1U) != 0U && !hold) {
            const auto device = (address >> 21U) & 31U;
            const auto reg = (address >> 16U) & 31U;
            data = 0xFFFFU;
            if (device == phy) {
                switch (reg) {
                case 2: data = 7; break;
                case 3: data = 0xC131; break;
                case 1: data = 4; break;
                case 0: data = 0x1000; break;
                case 31: data = 0x1018; break;
                default: break;
                }
            }
            address &= ~1U;
        }
        return address;
    }
    std::uint32_t read_data() const { return data; }
    void write_address(std::uint32_t word)
    {
        address = word;
        writes += 1U;
        if (hold_after_write) hold = true;
    }
};
struct Fixture {
    Clock clock;
    Board board{clock};
    Registers registers;
    h755::ethernet::MdioReader<Registers> reader{registers, *h755::ethernet::MdioClock::from_hclk(64'000'000)};
    volatile h755::BringupDiagnostics boot{};
    volatile h755::LinkDiagnostics link{0xFFFFFFFFU, 0, 0};
    h755::Diagnostics diagnostics{boot, link};
    h755::Application<Board, Clock, decltype(reader)> app{board, clock, reader, diagnostics};
    void advance(std::uint32_t milliseconds)
    {
        for (std::uint32_t i = 0; i < milliseconds; ++i) {
            clock.tick += 1U;
            app.poll();
        }
    }
};
void happy_path()
{
    Fixture f;
    f.app.poll();
    check(f.registers.writes == 0 && f.boot.magic == 0, "poll before start has no side effects");
    check(f.app.start().has_value(), "application starts");
    check(f.boot.state == 4 && f.boot.phy_address == 3, "actual MDIO reader discovers PHY");
    check(f.boot.phy_id1 == 7 && f.boot.phy_id2 == 0xC131, "actual PHY identifiers recorded");
    check(f.board.configured && !f.board.red_led, "board initialized; success LED policy");
    check(f.boot.m4_marker == 0x5041524BU, "M4 boot observation retained");
    check(f.clock.tick >= 200, "PHY startup delay honored");
    check(!f.app.start(), "cannot initialize running owner twice");
    f.advance(260);
    check(f.link.state == 5 && f.link.samples == 1 && f.link.errors == 0, "link monitor integrates with application");
    f.advance(2739);
    check(f.boot.heartbeat == 0 && !f.board.green_led, "LED stays off through 2999ms");
    f.advance(1);
    check(f.boot.heartbeat == 1 && f.board.green_led, "heartbeat and LED at 3000ms");
    f.advance(2999);
    check(f.boot.heartbeat == 1 && f.board.green_led, "LED stays on until next 3000ms boundary");
    f.advance(1);
    check(f.boot.heartbeat == 2 && !f.board.green_led, "next heartbeat turns LED off at 6000ms");
}
void startup_errors()
{
    Fixture bad_clock; bad_clock.board.hz = 0;
    check(!bad_clock.app.start() && !bad_clock.board.configured, "wrong clock rejected before hardware setup");
    Fixture bad_tick; bad_tick.clock.available = false;
    auto tick = bad_tick.app.start();
    check(!tick && tick.error() == h755::FaultCode::timebase, "timebase failure propagated");
    Fixture absent_core; absent_core.board.parked = false;
    auto core = absent_core.app.start();
    check(!core && core.error() == h755::FaultCode::m4_timeout, "M4 wait is bounded");
    Fixture absent_phy; absent_phy.registers.phy = 32;
    check(absent_phy.app.start().has_value(), "missing PHY is an expected external outcome");
    check(absent_phy.boot.state == 5 && absent_phy.board.red_led, "missing PHY is visible");
    const auto writes = absent_phy.registers.writes;
    absent_phy.advance(6000);
    check(absent_phy.registers.writes == writes && absent_phy.boot.heartbeat == 2, "missing PHY cannot stop heartbeat or issue invalid MDIO");
    Fixture timeout; timeout.registers.hold_after_write = true;
    check(timeout.app.start().has_value(), "MDIO timeout does not masquerade as a CPU fault");
    check(timeout.boot.mdio_errors == 1 && timeout.boot.state == 5, "startup timeout recorded");
}
void runtime_errors_and_rollover()
{
    Fixture f; check(f.app.start().has_value(), "runtime probe starts");
    f.advance(260);
    f.registers.hold_after_write = true;
    f.advance(270);
    check(f.link.state == 0xFFFFFFFFU && f.link.errors == 1, "transport timeout invalidates old link-up");
    Fixture busy; check(busy.app.start().has_value(), "begin-failure probe starts");
    busy.advance(260);
    busy.registers.address = 1U;
    busy.registers.hold = true;
    busy.advance(250);
    check(busy.link.state == 0xFFFFFFFFU && busy.link.errors >= 1U, "begin failure must invalidate stale link-up");
    Fixture wrapped; wrapped.clock.tick = 0xFFFFF800U;
    check(wrapped.app.start().has_value(), "rollover probe starts");
    wrapped.advance(2999);
    check(wrapped.boot.heartbeat == 0 && !wrapped.board.green_led, "rollover cannot trigger heartbeat early");
    wrapped.advance(1);
    check(wrapped.boot.heartbeat == 1 && wrapped.board.green_led && wrapped.clock.tick < 3000U,
          "3000ms heartbeat crosses counter rollover");
}
std::uint32_t tick_cycles{};
unsigned tick_stops{};
bool accept_tick{true};
void timebase_and_diagnostics()
{
    SysTick_Handler();
    {
        h755::Timebase time;
        check(!time.start(0) && !time.start(999) && !time.start(1001), "invalid timebase rates rejected");
        accept_tick = false;
        check(!time.start(64'000'000), "hardware setup failure releases ISR owner");
        accept_tick = true;
        check(time.start(64'000'000) && tick_cycles == 64'000 && time.now() == 0, "millisecond reload configured");
        h755::Timebase second;
        check(!second.start(64'000'000), "second instance cannot steal SysTick");
        SysTick_Handler(); SysTick_Handler();
        check(time.now() == 2, "ISR updates the bound owner");
    }
    check(tick_stops == 1, "timebase lifetime disables interrupt source once");
    SysTick_Handler();
    h755::record_fault(6U);
    check(g_bringup.fault == 6U && g_bringup.state == 0xFFFFFFFFU, "fault recording independent of application lifetime");
}
void gpio_policy()
{
    struct Port {
        std::uint32_t MODER{0xFFFFFFFFU}, OTYPER{0xFFFFU}, OSPEEDR{}, PUPDR{0xFFFFFFFFU}, BSRR{};
        std::array<std::uint32_t, 2> AFR{0xFFFFFFFFU, 0xFFFFFFFFU};
    } port;
    h755::gpio::alternate<2, 11>(port);
    check(port.MODER == 0xFFFFFFEFU && port.OTYPER == 0xFFFBU, "AF mode changes only owned pin fields");
    check(port.OSPEEDR == 0x30U && port.PUPDR == 0xFFFFFFCFU && port.AFR[0] == 0xFFFFFBFFU, "AF11 speed/pull/mux policy");
    h755::gpio::alternate<9, 7>(port);
    check(port.AFR[1] == 0xFFFFFF7FU, "upper AF register selected correctly");
    h755::gpio::configure_leds(port);
    check(port.BSRR == 0x40010000U && (port.MODER & 0x30000003U) == 0x10000001U, "LED setup starts low");
    h755::gpio::set_green(port, true); check(port.BSRR == 1, "green set");
    h755::gpio::set_green(port, false); check(port.BSRR == 0x10000U, "green reset");
    h755::gpio::set_red(port, true); check(port.BSRR == 0x4000U, "red set");
    h755::gpio::set_red(port, false); check(port.BSRR == 0x40000000U, "red reset");
}
}
namespace h755::processor {
bool start_tick(std::uint32_t cycles) noexcept { tick_cycles = cycles; return accept_tick; }
void stop_tick() noexcept { tick_stops += 1U; }
}
int main()
{
    happy_path(); startup_errors(); runtime_errors_and_rollover(); timebase_and_diagnostics(); gpio_policy();
}
