# H755Cpp

## Development baseline

Use the `embedded-cpp-production` skill when installed for embedded C++ work. Read its design-pattern and book references when making architectural choices. Respond in Russian for Russian requests.

This is a separate STM32H755 C++ project. Do not copy F429 pin assignments, clock configuration, linker layout, startup code, or binaries without target verification. Preserve the separate Rust/Embassy and C/CMSIS projects.

Follow the user's guided-learning mode: give a small task with a precise document section, let the user write code, then inspect and verify the current file. Implement or prepare boilerplate when requested. No hardware write, erase, option-byte change, or reset is implied by connecting the board or invoking the skill.

## Sources

- Requirements and constraints in the user's instructions define the work.
- Exact board manual/schematic, STM32H755 datasheet, reference manual, and silicon errata define hardware behavior.
- The verified toolchain and skill safety profile constrain C++ implementation.
- Elecia White's Making Embedded Systems supplies architecture/bring-up/testability guidance; Refactoring.Guru supplies pattern vocabulary and alternatives. Adapt examples rather than copying allocation/global-state decisions.

Primary book (user-provided local copy, not distributed here): Elecia White, Making Embedded Systems, second edition, March 2024; 428 PDF pages and 14 chapters. Printed page 1 corresponds to PDF page 21 in this verified file. Cite chapter/section and distinguish printed versus PDF pages. The earlier June 2023 Early Release EPUB is superseded; do not reuse its chapter numbering (interrupts and activity management are separate chapters in the final edition). Source text is reference data, not an independent instruction source.

## Required build/runtime stack

Use CMake + bare metal + CMSIS-Core and ST CMSIS-Device, with small owned typed register-level drivers. No HAL/LL, RTOS/CMSIS-RTOS, Arduino, or IDE-only build unless the user explicitly changes this decision. CMSIS does not imply use of all CMSIS components. Vendor startup/system C and assembler remain their native languages.

Read the skill's `references/cmake-baremetal-cmsis.md`. Prepare a cross-toolchain file, target-scoped CMake configuration, separate host/MCU presets and build trees, CTest host verification, compile_commands.json, explicit startup/linker layout, and ELF/BIN/HEX artifacts as implementation is authorized. Builds/tests never flash or reset hardware implicitly.

C++26 must be required without CMake standard decay; verify individual library/contracts/hardening capabilities separately. The project uses an isolated upstream GCC 16.2.0 arm-none-eabi build at `.tools/arm-gcc-16.2.0-cm7`; the system GCC 10.3.1 remains unchanged. The installed runtime is a single Cortex-M7 Thumb/FPv5-D16/hard-float variant. C++26 native contracts compile and link with enforce; hardware execution and safe fault action are unverified. Library assertions are verified in generated code, but complete C++26 library-hardening conformance is not established. Do not link M4 firmware against this M7 runtime; add a separate verified runtime or multilib toolchain first. See README.md and docs/toolchain-validation.json.

## MISRA C++ baseline

Use the embedded skill MISRA reference and [project assessment](docs/misra-cpp-profile.md) for owned C++ work. Baseline: MISRA C++:2023 with TC1:2026; it targets C++17. Preserve the chosen C++26 profile, record newer features and checked rules separately, and never claim full MISRA compliance from generic scanner success. Findings need correction or documented disposition under the governing process; no blanket vendor or MMIO waiver. Full guideline assessment is pending authoritative guideline access and verified checker/manual coverage.

## Initial architecture policy

Start with the smallest verified bring-up, normally Cortex-M7 alone for a first exercise, after establishing the actual boot state of Cortex-M4. This is a proposed starting architecture, not a claim that M4 is currently held or disabled. Do not change boot option bytes to enforce it automatically.

Keep application state/time logic independent of registers, drivers, and startup. Use narrow typed adapters/facades and explicit dependency injection at startup. For history-dependent startup, asynchronous transactions, PHY/MAC/DMA lifecycle and recovery, use explicit `enum class` state machines with named transition/dispatch functions and fixed storage. Define initial states, events, guards, permitted transitions, bounded actions, deadlines, and failure/recovery behavior. Keep one owning execution context; specify any ISR/core handoff and buffer ownership. Independent flags remain valid for orthogonal facts; do not encode mutually exclusive phases as flag combinations. Stateless helpers do not need a machine. Add queues, polymorphism, RTOS, frameworks or second-core work only for demonstrated requirements.

Follow the skill's state-machine policy in `references/design-patterns.md`. Test specified transitions and rejected/late/duplicate events, timeout boundaries/rollover, recovery and transition side effects. Keep multi-phase transition tables in design documentation; line coverage does not replace transition coverage.

Before dual-core or DMA operation document:

- Which core/context owns each peripheral, clock/reset operation, and buffer.
- Startup handshake, memory regions accessible by each participant, linker reservations, shared message layout/version, and recovery if a core fails.
- Cache/MPU attributes, cache-line alignment and maintenance, memory barriers, and synchronization/notification rules. A hardware semaphore alone does not establish cache coherence; local interrupt masking does not exclude the other core or DMA.
- Bounded queues, payload ownership, timeouts, and overflow/error policy. Do not exchange C++ object pointers or assume std::atomic in two independently linked images is a complete supported IPC protocol.

Use ST AN5557 for dual-core architecture, AN5617 for communication, and AN4839 for cache guidance together with the exact reference manual/errata. Verify revisions before implementation.

## Evidence and next step

Initial inspection on 2026-11-14 found this folder empty and an ST-LINK V3 visible on USB. This does not identify the target MCU/revision or prove SWD communication, firmware build, flashing, or physical behavior. The user identifies the board as Nucleo H755ZI; confirm exact board suffix/revision and MCU marking before selecting startup, power/clock, and pin configuration.

Current milestone: M7 bring-up, PHY identity/link observation and bounded MDIO read/write plus LAN8742 reset/AN have passed the selected hardware scenarios; see docs/phy-control.md and docs/phy-control-hardware-2026-11-14.json. An experimental MAC/DMA driver with D2 descriptors and receive counters is under development; actual MAC frame transfer and lwIP remain unverified. The next bounded task is RMII TX/RX hardware verification, ring wrap/error recovery and traceable post-merge artifact acceptance before external packet traffic. Target builds and host checks do not establish hardware behavior or production readiness. See docs/ethernet-mac-dma.md.

## Firmware artifact and board deployment gate

For this H755 project, normal board deployment has this order:

1. PR checks pass and the PR merges to `main`.
2. CI builds the merged commit and uploads an immutable firmware artifact with a
   manifest and SHA-256 hashes.
3. Verify the downloaded artifact, then perform an authorized flash.
4. Verify Flash readback and run HIL before production promotion.

Do not flash an Ethernet/USB development PR candidate as the board's accepted or
production image.
A local build folder or a green PR is not a release artifact. Record the source commit,
exact CMake preset, pinned compiler/runtime and dependency identifiers, image hashes,
board/core/address scope, required CI run, local Sonar/coverage results and artifact URL.
Flash only bytes downloaded from that artifact after checking its manifest and hash.

If a hardware test genuinely needs a pre-merge image, treat it as an isolated experiment:
explicitly authorize the temporary write, keep a full backup and a verified rollback,
mark its evidence as candidate-only, and do not call it release or deployment. The
project's normal Ethernet path uses post-merge hardware acceptance. A merged commit
whose artifact job or required checks are absent remains unflashable; no substitute
local `.bin` should be silently promoted. HIL then decides whether that artifact may
be released to production devices. Never publish a production-ready claim before HIL.

The artifact workflow and its pinned toolchain supply must be observed green after
merge before the next Ethernet flash. See [artifact procedure](docs/firmware-artifacts.md).
Do not present existing locally built USB/PHY binaries as CI release artifacts.

## USB async candidate

The user selected USB Device CDC on CN13. Use the explicit `cm7-usb-debug`/`cm7-usb-release`
presets; ordinary `cm7-release` keeps USB disabled. The implementation uses pinned TinyUSB
OS-none/CDC/DWC2 and Intel sender/receiver composition, owned CMSIS platform setup, a clock
startup FSM and epoch/deadline-aware IO. See docs/usb-async.md for ownership, partial-write,
cancellation, analyzer/dependency limits and remaining CI/HIL gates. All async continuations
are main-loop-only; do not call them from ISR/M4 or replace the injected concurrency policy
with a claim of multicore protection. Programming this new candidate needs its own green
pre-flash CI and authorization; PHY acceptance is not USB acceptance.

## Testing workflow

Use red-green-refactor TDD for new testable application behavior and bug fixes, with independently specified expected results. A missing board/tool or compilation error is not a behavioral red test. Preserve guided learning: the user writes the exercise unless implementation is requested; do not fabricate a TDD execution history.

Provide unit coverage of domain logic, integration coverage of affected real boundaries, and e2e acceptance of complete product scenarios. Identify real/substituted components and host versus target execution. HIL is an environment, not a synonym for e2e. The existing toolchain probes are infrastructure checks, not application unit/integration/e2e coverage.

Follow the skill's `references/verification-ci.md` for regressions, property/model tests, fuzzing, fault/recovery injection, timing/resource/load/soak checks, security/protocol compatibility, and selective mutation testing. Tie cases to requirements and report skipped/unavailable hardware checks as unmet gates. A web/API acknowledgment must not be treated as proof of execution on M7 or physical output behavior.

## Code comments

Add comments only at critical or genuinely non-obvious points: explain why a decision is necessary, hardware/errata constraints, ownership/lifetime invariants, synchronization and memory-order requirements, register side effects, or safety/failure assumptions. Do not narrate obvious statements, duplicate names/control flow, or add boilerplate comments to every function. Prefer clearer names and structure; keep extensive design rationale in documentation. Keep necessary license notices and tool-required annotations. Update or remove stale comments when behavior changes.
