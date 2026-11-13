#pragma once
#include "board.hpp"
#include "h755/application/controller.hpp"
#include "h755/ethernet/cmsis_mdio.hpp"
#include "h755/timebase.hpp"
namespace h755 {
using FirmwareMdio = ethernet::MdioReader<ethernet::CmsisMdioRegisters>;
using FirmwareApplication = Application<Board, Timebase, FirmwareMdio>;
[[noreturn]] void run_application(FirmwareApplication& application, Board& board) noexcept;
}
