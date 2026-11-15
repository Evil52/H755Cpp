#pragma once
#include "board.hpp"
#include "h755/application/controller.hpp"
#include "h755/ethernet/cmsis_mdio.hpp"
#include "h755/timebase.hpp"
#ifdef H755_ETHERNET_MAC
#include "ethernet/mac_runtime.hpp"
#ifdef H755_ETHERNET_LWIP
#include "h755/ethernet/lwip_network.hpp"
#include "h755/ethernet/lwip_echo.hpp"
#endif
#endif
#ifdef H755_ETHERNET_DIAGNOSTIC
#include "h755/ethernet/diagnostic_echo.hpp"
#endif
namespace h755::usb
{
    class Runtime;
}
namespace h755
{
    using FirmwareMdio = ethernet::MdioTransport<ethernet::CmsisMdioRegisters>;
    using FirmwareApplication = Application<Board, Timebase, FirmwareMdio>;
    struct FirmwarePeripherals
    {
#ifdef H755_USB_CDC
        usb::Runtime& usb;
#endif
#ifdef H755_ETHERNET_MAC
        ethernet::MacRuntime& mac;
#endif
#ifdef H755_ETHERNET_LWIP
        ethernet::LwipNetwork<ethernet::MacRuntime>& network;
        ethernet::LwipEcho& echo;
#endif
#ifdef H755_ETHERNET_DIAGNOSTIC
        ethernet::DiagnosticEcho& diagnostic;
#endif
    };
    [[noreturn]] void run_application(FirmwareApplication& application, Board& board,
                                      FirmwarePeripherals peripherals) noexcept;
}
