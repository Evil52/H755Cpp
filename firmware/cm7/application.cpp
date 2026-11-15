#include "application.hpp"
#include "h755/fault.hpp"
#ifdef H755_USB_CDC
#include "usb/runtime.hpp"
#endif
#ifdef H755_TARGET_COVERAGE
void coverage_poll();
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
        for (;;)
        {
            application.poll();
#ifdef H755_ETHERNET_MAC
            peripherals.mac.poll(application.uptime_ms(), application.link_mode());
            g_mac.state = static_cast<std::uint32_t>(peripherals.mac.state());
            g_mac.error = static_cast<std::uint32_t>(peripherals.mac.error());
            g_mac.received = peripherals.mac.received();
            g_mac.dropped = peripherals.mac.dropped();
            g_mac.transmitted = peripherals.mac.transmitted();
            g_mac.transmit_errors = peripherals.mac.transmit_errors();
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
