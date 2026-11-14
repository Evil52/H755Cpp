#include "usb_host_simulator.hpp"
#include "usb/runtime.hpp"
#include "h755/fault.hpp"
#include "h755/platform/processor.hpp"
#include "tusb.h"
#include "device/dcd.h"
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

using namespace usb_test;
namespace
{
    bool clock_ready{};
    unsigned clock_enables{};
    unsigned configurations{};
}
namespace h755::processor
{
    bool start_tick(std::uint32_t cycles) noexcept
    {
        return cycles == 64'000U;
    }
    void stop_tick() noexcept
    {
    }
}
namespace h755::fault
{
    [[noreturn]] void stop(FaultCode reason) noexcept
    {
        std::exit(reason == FaultCode::usb ? 73 : 74);
    }
}
namespace h755::usb
{
    void Platform::enable_clock()
    {
        ++clock_enables;
    }
    bool Platform::clock_ready() const
    {
        return ::clock_ready;
    }
    void Platform::configure()
    {
        ++configurations;
    }
    void Platform::disconnect()
    {
        irq_enabled = false;
    }
}
extern "C" void h755_usb_wait(volatile std::uint32_t*, std::uint32_t, std::uint32_t);
extern "C" void OTG_FS_IRQHandler();
extern "C" [[noreturn]] void __wrap_abort();
namespace
{
    void normal_runtime()
    {
        h755::Timebase clock;
        check(clock.start(64'000'000U), "runtime uses actual Timebase owner");
        {
            h755::usb::Runtime runtime{clock};
            check(runtime.start() && !runtime.start(),
                  "runtime binds exactly one clock/stack owner");
            runtime.poll();
            check(!tud_inited() && configurations == 0, "clock wait leaves USB inactive");
            {
                h755::usb::Runtime competing{clock};
                check(!competing.start(), "second runtime cannot steal global C ABI binding");
            }
            clock_ready = true;
            runtime.poll();
            check(tud_inited() && configurations == 1 && clock_enables == 1,
                  "ready clock enables actual TinyUSB once");
            {
                h755::usb::Runtime unstarted{clock};
            }
            check(tud_inited(), "destroying an unstarted runtime preserves active owner");
            h755::usb::TinyUsbPort observer;
            dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
            runtime.poll();
            configure(observer);
            for (unsigned i = 0; i < 4; ++i)
            {
                runtime.poll();
            }
            const std::array<std::byte, 4> bytes{std::byte{1}, std::byte{0}, std::byte{0xFF},
                                                 std::byte{2}};
            complete(2, bytes);
            std::vector<std::byte> output;
            for (unsigned i = 0; i < 10; ++i)
            {
                runtime.poll();
                SysTick_Handler();
                if (endpoint(0x82).pending)
                {
                    const auto& ep = endpoint(0x82);
                    for (unsigned j = 0; j < ep.size; ++j)
                    {
                        output.push_back(static_cast<std::byte>(ep.data[j]));
                    }
                    complete(0x82);
                }
            }
            check(output == std::vector<std::byte>(bytes.begin(), bytes.end()),
                  "production Runtime drives CDC echo with real Timebase/stack and async "
                  "composition");
            check(tusb_time_millis_api() == clock.now() && clock.now() == 10,
                  "TinyUSB C time API borrows the live owner");
            volatile std::uint32_t reg = 3;
            h755_usb_wait(&reg, 3, 3);
            OTG_FS_IRQHandler();
        }
        check(!tud_inited(), "runtime destruction unregisters operations before stack deinit");
    }
    void fatal_scenarios()
    {
        for (unsigned scenario = 0; scenario < 4; ++scenario)
        {
            const auto pid = fork();
            if (pid == 0)
            {
                if (scenario == 0)
                {
                    clock_ready = false;
                    h755::Timebase clock;
                    clock.start(64'000'000U);
                    h755::usb::Runtime runtime{clock};
                    runtime.start();
                    for (unsigned i = 0; i < 20; ++i)
                    {
                        SysTick_Handler();
                    }
                    runtime.poll();
                }
                else if (scenario == 1)
                {
                    volatile std::uint32_t reg = 0;
                    h755_usb_wait(&reg, 1, 1);
                }
                else if (scenario == 2)
                {
                    tusb_time_millis_api();
                }
                else
                {
                    __wrap_abort();
                }
                std::exit(1);
            }
            int status{};
            check(
                pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                    WEXITSTATUS(status) == (scenario == 3 ? 74 : 73),
                "clock timeout, stuck hardware and missing binding reach explicit fault boundary");
        }
    }
}
int main()
{
    normal_runtime();
    fatal_scenarios();
    return failures == 0 ? 0 : 1;
}
