# M7 Ethernet MAC/DMA candidate

The `cm7-ethernet-debug` and `cm7-ethernet-release` presets add an owned CMSIS
MAC/DMA driver to the existing USB CDC and PHY firmware. Ordinary M7 and USB
presets remain unchanged. The post-merge firmware artifact now selects the
Ethernet Release preset and verifies that both USB and Ethernet were enabled.
The separate `cm7-ethernet-coverage` preset builds a temporary instrumented image;
the PR CI compiles it but does not program a board.

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
polls at most four RX and four TX descriptors per pass. RX currently validates
single-descriptor frame status and length, counts it, then recycles the buffer;
there is no packet consumer yet. `send` copies one bounded frame to a TX buffer,
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

## Next acceptance steps

1. Review the DMA ring and relevant RM0399/errata details, including RX FCS
   length treatment, tail-pointer behavior and stop/restart timing. The host
   register-port test checks software transitions; only HIL can establish the
   actual STM32 status-bit and timing behavior.
2. For this explicitly authorized test candidate, pass PR CI first, including
   the instrumented target build. With a verified backup, collect target coverage
   from a temporary image and restore the exact previous Flash bytes. Combine
   fresh host, target and Python coverage, then run the local Sonar gate.
   The candidate is not a deployable Release image.
3. Merge only after acceptance. Then verify the immutable
   `cm7-ethernet-release` artifact and its hashes.
4. With separate authorization, flash that artifact and read back Flash. On the
   actual board verify D2 addresses, descriptor OWN transitions, RX counters,
   link loss/reconnect, USB CDC and heartbeat. TX needs a controlled frame
   producer and independent capture before acceptance.
5. Add the bounded `NO_SYS=1` lwIP adapter after MAC/DMA HIL; then verify ARP,
   ICMP, UDP/TCP and load against a real host.
