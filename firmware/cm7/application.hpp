#pragma once
#include "board.hpp"
#include "h755/application/controller.hpp"
#include "h755/ethernet/cmsis_mdio.hpp"
#include "h755/timebase.hpp"
namespace h755::usb
{
    class Runtime;
}
namespace h755
{
    using FirmwareMdio = ethernet::MdioTransport<ethernet::CmsisMdioRegisters>;
    using FirmwareApplication = Application<Board, Timebase, FirmwareMdio>;
#ifdef H755_USB_CDC
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board,
                                      usb::Runtime& usb) noexcept;
#else
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board) noexcept;
#endif
}
