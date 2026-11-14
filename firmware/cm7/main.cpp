#include "application.hpp"
#include "h755/fault.hpp"

int main()
{
    h755::Board board;
    h755::Timebase timebase;
    h755::Diagnostics diagnostics{g_bringup, g_link, g_phy};
    const auto clock = h755::ethernet::MdioClock::from_hclk(board.hclk_hz());
    if (!clock) h755::fault::stop(h755::FaultCode::clock);
    auto registers = board.mdio_registers();
    h755::FirmwareMdio mdio{registers, *clock};
    h755::FirmwareApplication application{board, timebase, mdio, diagnostics};
    h755::run_application(application, board);
}
