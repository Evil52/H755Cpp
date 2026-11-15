#include "application.hpp"
#include "h755/fault.hpp"
#ifdef H755_USB_CDC
#include "usb/runtime.hpp"
#endif
#ifdef H755_TARGET_COVERAGE
void coverage_poll();
#endif
#ifdef H755_ETHERNET_DIAGNOSTIC
extern "C"
{
    volatile std::uint32_t g_diagnostic_result{};
}
#endif
#ifdef H755_ETHERNET_LWIP
extern "C"
{
    volatile std::uint32_t g_network[5]{};
    volatile std::uint32_t g_echo[3]{};
}
#endif
namespace h755
{
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board,
                                      [[maybe_unused]] FirmwarePeripherals peripherals) noexcept
    {
        if (auto started = application.start(); !started)
        {
            fault::stop(started.error());
        }
#ifdef H755_USB_CDC
        if (!peripherals.usb.start())
        {
            fault::stop(FaultCode::usb);
        }
#endif
#ifdef H755_ETHERNET_MAC
        if (!peripherals.mac.start())
        {
            fault::stop(FaultCode::ethernet);
        }
#endif
#ifdef H755_ETHERNET_LWIP
        if (!peripherals.network.start(application.uptime_ms()))
        {
            fault::stop(FaultCode::ethernet);
        }
        if (!peripherals.echo.start())
        {
            fault::stop(FaultCode::ethernet);
        }
#endif
        for (;;)
        {
            application.poll();
#ifdef H755_ETHERNET_MAC
#ifdef H755_ETHERNET_DIAGNOSTIC
#ifdef H755_ETHERNET_LWIP
            peripherals.network.poll(application.uptime_ms(), application.link_mode(),
#else
            peripherals.mac.poll(application.uptime_ms(), application.link_mode(),
#endif
                                     [&](std::span<const std::byte> frame) noexcept
                                     {
                                         const auto result = peripherals.diagnostic.on_frame(
                                             frame, [&](std::span<const std::byte> reply) noexcept
                                             { return peripherals.mac.send(reply); });
                                         if (result != ethernet::DiagnosticResult::ignored)
                                         {
                                             g_diagnostic_result =
                                                 static_cast<std::uint32_t>(result);
                                         }
                                     });
#else
#ifdef H755_ETHERNET_LWIP
            peripherals.network.poll(application.uptime_ms(), application.link_mode());
#else
            peripherals.mac.poll(application.uptime_ms(), application.link_mode());
#endif
#endif
            g_mac.state = static_cast<std::uint32_t>(peripherals.mac.state());
            g_mac.error = static_cast<std::uint32_t>(peripherals.mac.error());
            g_mac.received = peripherals.mac.received();
            g_mac.dropped = peripherals.mac.dropped();
            g_mac.transmitted = peripherals.mac.transmitted();
            g_mac.transmit_errors = peripherals.mac.transmit_errors();
#ifdef H755_ETHERNET_LWIP
            g_network[0] = peripherals.network.address_ready() ? 1U : 0U;
            g_network[1] = peripherals.network.ipv4_address();
            g_network[2] = peripherals.network.received_frames();
            g_network[3] = peripherals.network.rejected_frames();
            g_network[4] = peripherals.network.transmitted_frames();
            g_echo[0] = peripherals.echo.udp_echoes();
            g_echo[1] = peripherals.echo.tcp_echoes();
            g_echo[2] = peripherals.echo.rejected();
#endif
            if (peripherals.mac.state() == ethernet::MacState::fault)
            {
                fault::stop(FaultCode::ethernet);
            }
#endif
#ifdef H755_USB_CDC
            peripherals.usb.poll();
#endif
#ifdef H755_TARGET_COVERAGE
            coverage_poll();
#endif
            board.idle();
        }
    }
}
