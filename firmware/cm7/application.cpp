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
#ifdef H755_USB_CDC
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board,
                                      usb::Runtime& usb) noexcept
#else
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board) noexcept
#endif
    {
        if (auto started = application.start(); !started)
        {
            fault::stop(started.error());
        }
#ifdef H755_USB_CDC
        if (!usb.start())
        {
            fault::stop(FaultCode::usb);
        }
#endif
        for (;;)
        {
            application.poll();
#ifdef H755_USB_CDC
            usb.poll();
#endif
#ifdef H755_TARGET_COVERAGE
            coverage_poll();
#endif
            board.idle();
        }
    }
}
