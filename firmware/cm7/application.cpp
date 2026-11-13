#include "application.hpp"
#include "h755/fault.hpp"
#ifdef H755_TARGET_COVERAGE
void coverage_poll();
#endif
namespace h755 {
[[noreturn]] void run_application(FirmwareApplication& application, Board& board) noexcept
{
    if (auto started = application.start(); !started) fault::stop(started.error());
    for (;;) {
        application.poll();
#ifdef H755_TARGET_COVERAGE
        coverage_poll();
#endif
        board.idle();
    }
}
}
