#include "application.hpp"
#include "h755/fault.hpp"
#ifdef H755_ETHERNET_MAC
#include "ethernet/mac_runtime.hpp"
#include <bit>
#endif
#ifdef H755_USB_CDC
#include "usb/runtime.hpp"
#endif

int main()
{
    h755::Board board;
    h755::Timebase timebase;
    h755::Diagnostics diagnostics{g_bringup, g_link, g_phy};
    const auto clock = h755::ethernet::MdioClock::from_hclk(board.hclk_hz());
    if (!clock)
    {
        h755::fault::stop(h755::FaultCode::clock);
    }
    auto registers = board.mdio_registers();
    h755::FirmwareMdio mdio{registers, *clock};
    h755::FirmwareApplication application{board, timebase, mdio, diagnostics};
#ifdef H755_ETHERNET_MAC
    static h755::ethernet::DmaStorage mac_storage __attribute__((section(".eth_dma"), used));
    auto const* uid = std::bit_cast<volatile const std::uint32_t*>(std::uintptr_t{UID_BASE});
    const std::array<std::uint8_t, 6U> mac_address{0x02U,
                                                   static_cast<std::uint8_t>(uid[0U]),
                                                   static_cast<std::uint8_t>(uid[0U] >> 8U),
                                                   static_cast<std::uint8_t>(uid[1U]),
                                                   static_cast<std::uint8_t>(uid[2U]),
                                                   static_cast<std::uint8_t>(uid[2U] >> 8U)};
    h755::ethernet::CmsisMacPort mac_port{*ETH};
    h755::ethernet::MacRuntime mac{mac_port, mac_storage, mac_address};
#endif
#ifdef H755_USB_CDC
    h755::usb::Runtime usb{timebase};
#endif
    h755::FirmwarePeripherals peripherals{
#ifdef H755_USB_CDC
        usb,
#endif
#ifdef H755_ETHERNET_MAC
        mac,
#endif
    };
    h755::run_application(application, board, peripherals);
}
