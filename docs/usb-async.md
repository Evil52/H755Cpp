# USB CDC on M7 with Intel async

This milestone implements a USB full-speed CDC-ACM device and a binary echo application.
It uses pinned TinyUSB 0.21.0 for USB enumeration, CDC, FIFOs and the STM32 DWC2 driver;
owned C++ provides the CMSIS board adapter, lifecycle, sender/receiver operations and
composition. There is no HAL, LL, RTOS, C++ coroutine frame or dynamic task allocation.
This is Intel sender/receiver async, not an implementation of the whole Embassy API.

## Boundaries and ownership

| Module | Responsibility |
| --- | --- |
| `include/h755/usb/cdc.hpp` | Main-loop-only async wait/read/write, operation lifetime, session epochs and deadlines |
| `include/h755/usb/echo.hpp` | Actual Intel wait/read/write/error/repeat composition |
| `include/h755/usb/startup.hpp` | Testable clock/startup FSM and finite register observation budget |
| `src/usb/tinyusb_port.cpp` | Bounded CDC FIFO adapter, reset/unplug and DTR session epochs |
| `src/usb/descriptors.cpp` | Explicit little-endian CDC/IAD descriptors and bounded ASCII-to-UTF16 strings |
| `firmware/cm7/usb/runtime.cpp` | Composition, Timebase binding, ISR/C ABI/fault hooks, teardown and diagnostics |
| `firmware/cm7/usb/platform.cpp` | CMSIS RCC/PWR/CRS/GPIO setup and safe USB disconnect |
| `tests/usb_host_simulator.*` | Substituted controller boundary for real TinyUSB/async host integration |

M7 owns USB2 OTG FS, its IRQ, HSI48, CRS and the configured pins. M4 remains the
unchanged parking image. The CPU handles the USB FIFO; DWC2 DMA is disabled.
Caller buffers are never loaned to DMA. The existing CPU/AHB clock remains 64MHz.
No second Timebase or SysTick owner is introduced.

Intel continuations and stop/critical-section machinery run only in the main loop.
The explicit injected concurrency policy provides no cross-thread/core protection:
it is valid under this single-owner rule. The USB ISR handles the bounded DWC2 work
and queues TinyUSB events; it never runs application sender continuations.
Reset/unplug epoch publication uses a lock-free atomic. CDC control callbacks run
in the main-loop task; DTR changes also invalidate the old session.

There is one fixed registration slot each for wait-ready, read and write.
Operation objects do not move, and must outlive pending work. Their destructor
unregisters pending callbacks before destroying the receiver. The device and port
must outlive pending operation objects. Completion releases its slot first and does
not access the operation/device after invoking the receiver. A successful completion
may therefore destroy it when the other registrations have already been quiesced.

## State and transition policy

| Machine/state | Event or guard | Result |
| --- | --- | --- |
| Startup `idle` | start | Enable HSI48; enter `waiting_clock` |
| `waiting_clock` | ready before 20ms | Configure hardware, initialize TinyUSB once; enter `running` |
| `waiting_clock` | deadline or stack initialization failure | Enter `failed`; runtime records reason and invokes USB fault policy |
| CDC `offline` | host configures device | `configured` |
| `configured` | DTR asserted | `ready` |
| `ready` | suspend | `suspended`; pending IO retains its deadline |
| `suspended` | resume | Re-evaluate configuration/DTR; continue pending IO within its budget |
| Any CDC state | reset/unplug or DTR change | Increment epoch; old transfer operations complete with reset/error |
| Operation `created` | start and free lane | Register stable node, enter `queued`; normal completion is deferred |
| `queued` | data/TX capacity/ready, healthy epoch and budget | Progress one bounded chunk or complete once |
| `queued` | deadline/error/cancel | Release slot, enter `completed`, publish explicit error/stopped |
| `queued` | operation destruction | Release registration without calling a destroyed receiver |

Starting an already started operation or destroying a device with registered work
is an internal invariant violation with a stopping action. Busy lanes are explicit
errors. Timeout must be in `[1, 0x7fffffff]` milliseconds; subtraction is unsigned
and wrap-safe within that interval. A deadline takes precedence over newly observed
readiness. Suspend does not pause the timeout.

Queued controller events are processed before caller IO; a published reset cannot
cause bytes from the old FIFO/session to be accepted by a new transfer. A close/open
DTR sequence between two polls still changes the epoch. Repeating the same DTR value
does not cancel healthy operations.

## API semantics and resource bounds

`wait_ready(timeout)`, `read_some(span, timeout)` and `write_all(span, timeout)` return
Intel-compatible senders. They compose with `async::connect/start`, `then`, `let_value`,
`let_error` and `repeat`. `Operation::cancel()` is cooperative and completes stopped
on a subsequent poll. Generic stop-token propagation into these custom operations is
not implemented; do not assume the full P2300 cancellation contract.

The caller owns each buffer and keeps it valid, stable and unmodified until completion
or removal of its registration. The echo pipeline owns one 64-byte buffer. Reads return
at most 64 bytes. A write retains its offset across TX backpressure and copies at most
64 bytes per dispatch. There is at most one application completion per poll.

Write success means all bytes were copied into the bounded USB transmit path. It does
not mean that the host application received/processed them. An error includes the
already queued prefix length. Cancellation cannot retract an already queued prefix;
do not blindly resend a whole command after a partial write.

The TinyUSB event queue has 16 entries, its main-loop task processes at most four events
per call, and the local DWC2 patch drains at most four RX packets per IRQ. Reset, FIFO
flush and endpoint-disable waits have a 65,536-register-read ceiling. The ISR/dispatch
cycle cost and worst-case masking time remain unmeasured. The active controller is FS;
the copied driver code is not a qualified HS implementation.

A hardware/stack invariant failure disables USB interrupts, masks controller interrupts,
soft-disconnects the device and enters the existing safe fault stop (red LED). Clock and
stack startup errors use the explicit startup error state first. `abort` is routed to
the reviewed assertion fault stop, including Debug; it must not pull malloc/syscalls
into the firmware. Independent watchdog/SysTick-failure detection remains unimplemented.

## Board wiring and build profiles

The user selected Device CDC on the user USB connector CN13, separate from ST-LINK CN1.
UM2408 Rev6 section 7.11/table13 maps PA9 to VBUS, PA11 to DM, PA12 to DP, and PD10/JP8
to the host power switch. Device mode holds PD10 low and never sources host VBUS.
HSI48 feeds USB; CRS synchronizes to USB2 SOF. Hardware VBUS sensing is enabled.
The actual solder bridges, electrical levels, clock lock and behavior still need HIL.

**Power the board through CN1/another supported supply before attaching CN13.**
CN13 cannot power this board; ST documents an IO current-injection risk with the reverse
order. Keep that power connection while using the user USB device port.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
cmake --preset cm7-usb-debug
cmake --build --preset cm7-usb-debug
cmake --preset cm7-usb-release
cmake --build --preset cm7-usb-release
```

`H755_USB_CDC=ON` is explicit in these two candidate presets. Ordinary `cm7-release`
keeps USB disabled. Its rebuilt image differs from the accepted PHY image only in two
native-contract source-location bytes (processor.cpp line/column after formatting).
That rebuilt image is not claimed as a newly accepted/programmed Release. The exact
previously programmed image remains archived and on the board. All configure/build/test
commands are free of board writes/resets.
The lab identifiers are TinyUSB example VID/PID CAFE:4001, with no serial descriptor;
a released product needs its own identifier/provisioning decision.

## Evidence and remaining gates

See `usb-async-validation-2026-11-14.json` for measured coverage, hashes and tool scope.
Host integration uses real EP0/CDC/FIFO and Intel composition with a fake DCD, including
binary packets around 64-byte boundaries, DTR reopen, reset, ownership and safe teardown.
A separate test runs the production runtime with real Timebase logic and substituted
clock/GPIO/controller services. Controlled host processes verify clock/stuck-register/
missing-binding/abort fault boundaries. These do not establish actual MCU fault behavior.

Clang analyzes the new C++ through real compilation commands. Cppcheck's existing portable
application scope remains enabled; its parser rejects Intel stdx C++20/26 constructs.
Those rejected experimental runs are retained locally and are not reported as green USB
Cppcheck analysis. Sonar Community CXX is grammar/metrics plus imported findings/coverage;
missing compiler/standard-header context remains a limitation even with zero parser errors.

CI on implementation commit `5178ef7` passed all seven jobs in
[run43611665262](https://github.com/Evil52/H755Cpp/actions/runs/43611665262).
CodeQL C++, Python and Actions report zero results and no analysis errors on that exact
commit. Documentation-only follow-up reuses this evidence with unchanged runtime/build
inputs and exact Release hashes. Host CodeQL extraction does not cover target-only CMSIS
platform glue. Authorized programming, host USB enumeration and COM-port exchange on the
real board remain required before hardware acceptance. Test
connected/no-cable boot, exact release bytes, open/close, unplug/replug, suspend/resume,
reset/cold power cycle, full binary echo at 0/1/63/64/65/257 and longer lengths, timeout
and backpressure, continued PHY/LED behavior, clock/CRS/USB register masks and memory/stack
budgets. Preserve M4/options and the verified prior Release/rollback. Do not confuse the
existing ST-LINK virtual COM port with the new CN13 CDC device.

This is not complete USB conformance, a fuzz/soak campaign, measured WCET, full MISRA
compliance, automotive certification or proof of absence of UB/security issues.

Primary references:
- https://www.st.com/resource/en/user_manual/um2408-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf
- https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
- Pinned ST CMSIS STM32H755 definitions and the TinyUSB DWC2 STM32 implementation.
- https://intel.github.io/cpp-baremetal-senders-and-receivers/
- https://docs.tinyusb.org/en/latest/
