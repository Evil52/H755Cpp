# CM7 module boundaries

The composition root is firmware/cm7/main.cpp. It constructs one Board facade, Timebase,
Diagnostics sink, typed CMSIS MDIO adapter/reader and Application controller, then enters
the target scheduling adapter. Objects are stack-owned and outlive their borrowers.
Application, Timebase, MDIO and Board are noncopyable; there is no allocator or service locator.

| Module | Responsibility | Dependencies/test boundary |
| --- | --- | --- |
| include/h755/application/controller.hpp | Boot lifecycle, PHY discovery, heartbeat and link scheduling | Board/Clock/Mdio are compile-time injected ports; real MDIO/link state machines run against fake registers in host integration tests |
| firmware/cm7/application.cpp | Target run loop and optional coverage hook | Delegates application behavior and fatal startup outcomes |
| firmware/cm7/board.cpp | Nucleo pins/clocks, M4 observation, MDIO binding, LED facade | Exact pinned CMSIS definitions; raw GPIO field policies are separately host-tested |
| include/h755/board/gpio.hpp | Bounded compile-time pin/function encoding and LED register policies | Register-shaped test values validate preservation of unrelated bits; no fabricated physical evidence |
| src/timebase.cpp | Millisecond counter and explicit SysTick ISR owner lifetime | CPU setup is a narrow port; host tests exercise real owner binding, failure cleanup and destructor unbinding |
| firmware/cm7/processor.cpp | ARM instructions and SysTick register access | Target-only CMSIS port; native enforce contract checks its internal reload precondition |
| firmware/cm7/fault.cpp | Contract/assert/fault ABI hooks and safe stop | No dependency on constructed application/clock; disables interrupts before emergency LED setup |
| src/diagnostics.cpp | Stable debugger ABI and diagnostic updates | Layout/size assertions preserve g_bringup/g_link; real writer is host-tested with owned RAM snapshots |

The Adapter pattern is the existing CmsisMdioRegisters boundary. Board is a narrow Facade.
Application uses compile-time dependency injection rather than runtime polymorphism or a
Singleton. Its lifecycle is an enum (cold/starting/running/failed); active MDIO work and
LED output remain independent Boolean state. Templates live in headers to permit testing
with alternate ports. This is ordinary C++ translation-unit modularity, not a requirement
to use C++ module imports or introduce an interface/class hierarchy per file.

Board has one owner at startup. Diagnostics globals remain only for debugger ABI/fault
entry points, and normal behavior writes through the injected sink. Timebase's private
ISR binding is set before enabling SysTick and disabled before owner lifetime ends.
It is single-core/single-owner; these rules are not an M4/DMA synchronization protocol.
M4 remains the unchanged assembly parking image.

A startup-only bounded MDIO wait now idles between pending polls rather than spinning;
SysTick supplies elapsed time. The cooperative runtime poll has no busy-wait loop.
As before, independent detection of a failed SysTick/watchdog is not implemented.
Clock/profile failures are explicit startup errors, not contract predicates on external
input. The GPIO pin/AF range invariant is now compile-time checked. Fault stop needs no
Board object and can run before normal initialization.

A regression test exposed stale link-up when a new MDIO snapshot could not begin.
Application now invalidates that observation and records the error, just as it does for
poll failures. This intentional correction accompanies the structural refactor.

## Validation sequence

Host integration exercises boot/PHY/link/heartbeat, missing PHY, clock/timer/M4 failures,
MDIO timeout, stale-up rejection, unsigned rollover, GPIO preservation, the real Timebase
ISR binding and the real diagnostic writer. SDK address layout/electrical behavior still
requires target acceptance.

Fresh compiler coverage notes count target-only code as unexecuted until a new MCU run;
old M7 coverage is not reused across this refactor. Declaration-only headers have explicit
source-SHA reviews in coverage/declarations.json and contribute zero executable lines.
Changing their content invalidates that review. The unchanged M4 trace is reused only
with matching source and binary hashes. No executable firmware source is excluded.

The user explicitly approved a staged CI candidate because CI-before-flash and
MCU-coverage-before-commit otherwise form a dependency cycle. The test candidate may be
committed/pushed after local checks despite incomplete MCU coverage. Only after its CI
passes may the isolated coverage image run. The previously verified ordinary Release is
then restored. Final acceptance still requires >80% coverage and a clean local Sonar gate;
a green pre-hardware CI alone does not make this candidate a validated release.

## Measured result

Candidate d7ac19a passed all seven required PR CI jobs in run 43524451276 before
programming. Its isolated instrumented M7 image then read LAN8742 ID 0007:c131,
advanced heartbeat/link samples without MDIO/fault/monitor errors and reached
100 Mbit/s full duplex. The prior verified 3836-byte ordinary Release was restored;
all 2 MiB of Flash match its saved baseline exactly. M4 was not rewritten and
option/boot configuration was unchanged. The refactored ordinary Release is built,
but has not been deployed or accepted as the operational image.

Merged fresh host/MCU coverage is 365/406 C/C++ lines (89.90%) and 260/260 Python
lines (100%); no owned runtime source is unmeasured. The unchanged M4 trace covers
9/9 instructions. Six CTest groups and 29 Python tests pass. Local Sonar gate is OK,
with zero issues and 92.0% new-code coverage; its overall metric rounds to 93.8%.
Semgrep parsed 23 C++ files without findings/errors; CodeQL reported zero results
for C++, Python and Actions. See modular-validation-2026-11-14.json for hashes,
scope, evidence and limitations. Coverage is a line metric, not a correctness proof.

MISRA C++ baseline and the explicit C++26/compliance boundary are recorded in
misra-cpp-profile.md. No claim of full MISRA or automotive certification is made.
