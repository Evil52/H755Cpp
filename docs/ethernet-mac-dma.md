# M7 Ethernet MAC/DMA candidate

The `cm7-ethernet-debug` and `cm7-ethernet-release` presets add an owned CMSIS
MAC/DMA driver to the existing USB CDC and PHY firmware. Ordinary M7 and USB
presets remain unchanged. The post-merge firmware artifact now selects the
Ethernet Release preset and verifies that both USB and Ethernet were enabled.
The separate `cm7-ethernet-coverage` preset builds a temporary instrumented image
with the one-shot diagnostic responder; the PR CI compiles it but does not program
a board.

## Ownership and memory

- The M7 main loop owns the MAC/DMA state and all descriptor transitions. M4
  does not access these objects. No Ethernet ISR is enabled.
- `MacEngine` owns the queue lifecycle and accepts a narrow register port;
  `CmsisMacPort` owns the STM32 MMIO details. Both the port and storage outlive
  the engine in `main`. The host test uses the same engine with a fake port.
- Four 16-byte TX and four 16-byte RX descriptors, plus four 1536-byte buffers
  in each direction, occupy `.eth_dma` in D2 SRAM1 at `0x30000000`. The section
  is `NOLOAD`; `start_link` initializes descriptors before starting DMA.
- Descriptors and buffers are aligned to 32 bytes. The driver refuses to start
  if M7 D-cache is enabled. A future cache-enabled build needs explicit MPU or
  per-buffer maintenance and a new hardware acceptance run.
- DMA list and tail registers require 32-bit addresses. Pointer conversion is
  confined to the CMSIS adapter, guarded by a 32-bit `uintptr_t` assertion and
  a runtime D2-range/alignment check. This target-specific conversion needs a
  recorded MISRA assessment; no formal rule compliance is claimed.
- The board's RCC Ethernet reset occurs before PHY discovery. The MAC driver
  does not issue a second DMA software reset during an MDIO transaction.
- A locally administered unicast MAC is derived from the STM32 unique ID. The
  five selected ID bytes are not a collision-proof production address policy;
  fleet deployment needs provisioned unique addresses and duplicate detection.

## State and failure behavior

`cold -> ready -> running` follows board setup, driver configuration, and a
resolved PHY link. Link loss enters `stopping` and waits up to 50 ms for both
DMA process states in `DMADSR` to report stopped before returning to `ready`;
`DMACSR.TPS/RPS` are event flags and are not used as a state probe. A later link
result restarts the rings with its selected speed and duplex. The main loop
polls at most four RX and four TX descriptors per pass. RX validates
single-descriptor frame status and length, counts it, then passes the borrowed
frame to a synchronous callback before recycling the buffer.
It must not retain the `span` after the callback returns. There is no lwIP
packet consumer yet. `send` copies one bounded frame to a TX buffer,
rejects queue exhaustion, and publishes OWN only after a memory barrier. A
fatal DMA status or enabled D-cache latches `fault` and enters the board's
stopping fault path. This is an experimental state machine until live cable
loss/reconnect, DMA error, and ring wrap are observed.
`g_mac` exposes the state, error and frame counters for read-only SWD inspection.

The portable descriptor tests cover encodings, invalid lengths, ownership,
first/last and error status. The engine integration test covers ring wrap,
backpressure, mode change, cable loss, aborted TX, DMA error and stop timeout.
Application integration tests check that the MAC sees negotiated 100/full,
link down/reconnect and MDIO failure. GCC 16.2 target
Debug/Release builds verify the CMSIS register names and linker placement.
These checks do not prove real RMII packet traffic, DMA cache behavior, or TX
completion on the board.

## Isolated raw-frame probe

Only `cm7-ethernet-coverage` enables `H755_ETHERNET_DIAGNOSTIC`. It recognizes a
60-byte or longer untagged Ethernet frame addressed to the board or broadcast,
with local experimental EtherType `0x88B5`, payload bytes 14–21 `H755PING`, and
four sequence bytes at 22–25. After validating a unicast nonzero source address,
it makes at most one response attempt per boot: 60 bytes addressed to the
requester, sourced from the board MAC, with `H755PONG` and the same sequence.
No other traffic is echoed. The normal Release preset excludes this responder,
and the artifact packager rejects coverage or diagnostic builds. `0x88B5` is
[reserved for local experiments](https://www.iana.org/assignments/ieee-802-numbers);
this probe must remain on a network under the operator's control and must never
be treated as a product protocol. The target MAC sets automatic pad/CRC
stripping before the borrowed frame reaches the callback, following the
[ST Ethernet driver configuration](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_hal_eth.c).
The host probe is [ethernet_raw_probe.py](../tests/hil/ethernet_raw_probe.py):
it sends one bounded broadcast request through macOS libpcap and waits for a
matching unicast response. It requires access to a BPF device; the HIL run used
an identical SHA-256 copy under `/private/tmp` because the elevated process
could not read the project under macOS Documents privacy controls.

## CI-candidate hardware evidence

The earlier draft PR #13 revision passed its PR CI before an isolated
instrumented M7 run. The MAC reported `running`, zero errors and two received
descriptors; full Flash readback
after restoration matched both preflight backups. Whole-owned-code line coverage
was 92.4% C/C++ and 88.3% Python, with no unmeasured files. Local Sonar Quality
Gate passed with zero unresolved issues, subject to its documented C++ parser
limits. See [the exact-image HIL record](ethernet-mac-hil-2026-11-15.json).
Those results predate the RX callback and diagnostic responder in this revision;
the current revision `ea3530d` passed all eight PR checks, including target
builds, CodeQL and Semgrep. In a separate restored coverage run, the MAC stayed
`running` with zero error, 135 ambient RX frames and zero drops. A second run
with the same image sent one broadcast `H755PING` from the Mac. The host captured
a matching unicast `H755PONG` with the same sequence; M7 recorded one RX, one
completed TX, zero errors and diagnostic result `sent`. Both full-Flash
restorations matched the original readback. Whole-owned-code line coverage from
the second run was 93.1% C/C++ and 84.9% Python, with no
unmeasured files; local Sonar Quality Gate passed with zero unresolved issues.
The first community CXX run reported 726 missing standard-header includes;
the corrected scan with the pinned GCC 16.2 headers reported zero include
resolution and parser syntax errors. Its rules still do not establish full
C++26 semantic coverage. The scanner-helper change was retested at 85.2%
Python coverage; the M7 and host C++ execution reports were unchanged.
See [the current candidate HIL record](ethernet-rx-candidate-hil-2026-11-15.json).
This proves one bounded payload exchange and TX completion on the test image.
Ring wrap, link loss/reconnect and sustained packet load remain unverified.

## Next acceptance steps

1. Review the DMA ring and relevant RM0399/errata details, including RX FCS
   length treatment, tail-pointer behavior and stop/restart timing. The host
   register-port test checks software transitions; only HIL can establish the
   actual STM32 status-bit and timing behavior.
2. Extend the single-frame RMII test to ring wrap, queue pressure and cable
   loss/reconnect with independent host-side observations. Keep the current
   draft candidate separate from an accepted Release image.
3. Merge only after acceptance and separate authorization. Then verify the immutable
   `cm7-ethernet-release` artifact and its hashes.
4. With separate authorization, flash that artifact and read back Flash. On the
   actual board verify D2 addresses, descriptor OWN transitions, RX counters,
   link loss/reconnect, USB CDC and heartbeat. Repeat affected packet checks
   against the delivered Release image; the diagnostic responder is absent there.
5. Add the bounded `NO_SYS=1` lwIP adapter after MAC/DMA HIL; then verify ARP,
   ICMP, UDP/TCP and load against a real host.
