# LAN8742 link monitoring and whole-project coverage

## Behavior and ownership

The next bounded milestone is read-only link monitoring, not a complete Ethernet stack.
The monitor borrows the single-owner MDIO transport and a validated PHY address; both
outlive it. An enum state machine performs at most five transactions. It reads BMSR
(register 1) twice to clear latched-low link state, then BMCR (0) and, for auto-negotiation,
PHY special status (31). A final BMSR read rejects a link lost during mode sampling.
It reports down, negotiating, or 10/100 half/full duplex. Floating-bus values, reserved
resolved modes and current remote faults are errors; reset/power-down/isolate/loopback
are not published as an external link-up. MDIO errors never preserve a previous up state.

Each transaction has a checked timeout; polling must continue regularly. A transport
timeout does not cancel hardware. The caller owns explicit idle recovery, as before.
The main loop starts snapshots every 250 ms, polls without blocking the LED heartbeat,
and exposes g_link (state, successful samples, errors). It writes no PHY registers and
sends no frames. PHY reset/write/autonegotiation control, MAC/DMA and lwIP remain future work.

Source: [Microchip LAN8742A datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf),
registers 0, 1 and 31; the [ST PHY driver](https://github.com/STMicroelectronics/stm32-lan8742)
independently demonstrates the BMSR double-read sequence. No HAL/LL implementation is used.

## Verification and coverage scope

Behavioral red tests failed against an implementation stub, then passed after the state
machine was implemented. Additional red tests caught remote-fault/loopback edge cases.
Unit tests cover latch clearing, negotiation, all speed/duplex modes, link loss, invalid
status, pending work, transport errors and reuse. Integration tests use the actual
MdioReader with fake MAC registers, verify exact Clause 22 command words and exercise
fault latching plus explicit recovery. This is distinct from the physical PHY test.

LLVM source coverage measures host C++ execution; GCC 16 gcov measures an instrumented
M7 image. GCC -fprofile-info-section and __gcov_info_to_gcda serialize counters into a
bounded 64 KiB RAM buffer; no file I/O or heap allocation is used. This is isolated test
firmware, never the delivery image. Interrupts are masked briefly for a consistent snapshot;
this does not verify production latency. Allocation callback capacity failures stop the
capture; the callback is not required by the observed simple-counter profile.
The collection experiment uses finally to restore the saved, previously verified Release.
That restoration is deliberate: the newer ordinary Release build has not been flashed.

The physical instrumented run observed 100/full link, advancing samples, no monitor
errors and the expected PHY IDs. The M4's nine executable instructions (including the
parking fault loop) were checked via SWD single-step with PC validation. This directed
branch test is not a Cortex exception-injection test. The saved Release is restored after
collection; no option bytes are changed. Cable removal/reconnection and power-cycle
acceptance are not yet physically tested.

All owned runtime C/C++ and Python under include/src/firmware/tools are inventoried.
Tests, toolchain probe fixtures, vendor code, generated/build artifacts and declarative
CMake/configuration files are outside runtime line coverage. No owned runtime source may
be missing a report. The explicit-instantiation .cpp has no independent executable lines;
its instantiated bodies are measured in the headers. Header line hits are the union of
host and MCU runs, not a sum of percentages. Assembly instruction coverage is reported
separately and is not mixed into line counts.

The gate requires strictly >80% line coverage separately for C/C++ and Python, with all
assembly instructions observed. Python branch coverage is reported independently; C++
line coverage is not a claim of >80% branch/path coverage. Some fault/diagnostic paths
remain uncovered. Coverage reports must come from the exact current source: the aggregator
validates source inventory but does not itself rebuild tests or prove report freshness.

## Local commands

Use host-coverage CMake preset, run both mdio_tests modes and both link_tests modes with
LLVM_PROFILE_FILE, merge profiles per executable, then llvm-cov export -format=lcov.
Keep separate profiles to avoid unrelated internal-function name collisions.
Run Python tests with coverage.py 7.17.3 and .coveragerc; relative_files makes Cobertura
paths valid inside the scanner container. Vendor and probe fixtures remain excluded.
The MCU experiment uses an explicitly authorized instrumented image, gcov-tool merge-stream,
and gcov --json-format. It must be executed again if the instrumented source changes.

```sh
python -m coverage run -m unittest discover -s tests -p 'test_*.py'
python -m coverage combine
python -m coverage json -o build/python-coverage/coverage.json
python -m coverage xml -o build/python-coverage/coverage.xml
python tools/coverage_report.py \
  --lcov build/host-coverage/mdio.info build/host-coverage/link.info \
  --gcov build/target-gcov-json/main.gcov.json.gz build/target-gcov-json/coverage.gcov.json.gz \
  --python build/python-coverage/coverage.json \
  --assembly build/assembly-coverage.json --output build/coverage
python tools/sonar_local.py --env-file /path/to/authorized-local-sonar.env
```

Cppcheck 2.22.0 also analyzes the portable tests and firmware with 32-bit types and explicit
ARM/GCC/CMSIS defines. Two exact vendor-only diagnostics are recorded in
security/cppcheck/suppressions.txt: CMSIS's unused __cmsis_start compares linker-defined
boundaries. The linked firmware uses ST startup instead and contains no __cmsis_start.
There are no owned-code suppressions. Neither that parser nor sonar-cxx establishes full
C++26 semantic/security coverage. Semgrep policy rules cover the owned firmware as well;
CI clang-tidy/CodeQL continue to analyze the portable host subset.
