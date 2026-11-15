# M7 Ethernet: implementation boundary and verification

## Decision

Use an owned CMSIS register-level MAC/DMA/PHY driver with upstream lwIP in bare-metal
`NO_SYS=1` mode. No HAL/LL or RTOS. M7 is the initial owner of Ethernet; future migration
to M4 is a separate resource/IPC/runtime decision. Avoid a global singleton: inject
the peripheral adapter into a noncopyable transport owner and keep protocol logic testable.

## Clause 22 MDIO transport

- Distinct checked PHY/register address types reject values above 31 without truncation.
- HCLK-to-MDIO selection follows STM32H7 divider ranges and rejects zero or HCLK above
  the selected H755 profile's 240 MHz limit. Very low HCLK values are accepted by the
  encoder; the caller must choose a transaction deadline suitable for actual MDC frequency.
- Nonblocking `begin`/`poll` reads and `begin_write`/`poll_write` writes share one outstanding operation, with no allocation or busy-wait loop. See [PHY control](phy-control.md) for staging, verification and startup sequencing.
- Timestamps are unsigned 32-bit ticks supplied by one monotonic timebase. Timeouts must
  be nonzero and at most `0x7FFFFFFF`; polling must occur before a complete tick cycle is missed.
- A completed hardware operation wins when observed, including a late poll. A still-busy
  operation at its deadline times out, latches fault state, and requires explicit recovery
  only after hardware busy clears. Timeout does not pretend to cancel the hardware transaction.
- LAN8742A identifier matching ignores the silicon revision nibble; identifier matches
  alone do not establish board identity or distinguish every Microchip device sharing IDs.
- The CMSIS adapter checks owned bit encodings against pinned vendor definitions at compile time.

Caller obligations before MMIO: configure and verify peripheral/GPIO clocks, supply,
RMII pins/alternate functions and actual HCLK; ensure exclusive ownership, initialize
the timebase, and establish M4 boot state. Driver objects and their register adapter must
remain alive through the operation. This API is single-context; it is not an ISR/main-loop
or multicore synchronization primitive. Existing unrelated MDIO configuration must not
be shared concurrently: each command writes a complete Clause 22 address register value.

## Packet-pin and ring preparation

The M7 board setup now configures all nine RMII signals as AF11: PA1 clock, PA2 MDIO,
PC1 MDC, PA7 CRS_DV, PC4/PC5 RXD0/RXD1, PG11 TX_EN, PG13 TXD0 and **PB13 TXD1**.
PB13 is specific to MB1363's Ethernet route; PG12 is not this board's TXD1. Verify
JP6/JP7 and the solder bridges from UM2408 Table 14 on the actual board before enabling
MAC traffic. A target build confirms register definitions, not electrical connection.

`frame_ring.hpp` is a fixed-capacity, main-loop-owned state model. TX transitions
`free -> producer -> dma -> free`; `cancel` returns only a producer-owned slot to free.
RX transitions `dma -> consumer -> dma`. Invalid indices, lengths and transitions are
rejected; exhaustion returns `full` without overwriting an in-flight frame. A MAC adapter
may call TX `reclaim` only after checking that hardware has cleared descriptor OWN, and RX
`complete` only after validating descriptor status and frame length. `submit` models the
ownership handoff; the adapter still must publish descriptor writes with the required
barriers and cache maintenance before setting hardware OWN. The ring itself contains no
DMA descriptors, payload buffers, MMIO, interrupt synchronization or packet traffic.

Before enabling MAC/DMA, reserve descriptor and frame storage in DMA-accessible memory,
choose and verify cache/MPU treatment, align buffers to cache lines, specify descriptor
layout and register sequence from the exact RM0399 revision, and test recovery after DMA
errors. M7 main loop will remain the sole ring owner; ISR may only send a bounded
notification. M4 does not access this state.

## TDD and current evidence

The initial implementation stubs compiled successfully, then unit and register-adapter
integration tests failed on divider/PHY behavior. Implementing the state machine made
those tests pass under host ASan/UBSan. Target Debug/Release compile the real CMSIS adapter
and instantiate the transport. Test expectations include an independent exact command word,
range boundaries, timeout latching, busy rejection, tick wrap, and one-time result delivery.

`mdio_register_integration` uses fake registers. It checks cooperating software components,
not Ethernet electrical timing or DMA. The first [hardware bring-up](m7-bringup.md)
verified PHY identifiers and LED heartbeat. [Link monitoring](link-monitor-and-coverage.md)
adds a bounded snapshot state machine. Packets/ping/e2e remain unverified.

## Remaining milestones

1. Verify exact Nucleo/MCU revision, jumpers and clock/power/boot state; implement board startup,
   linker map, timebase and MDIO/RMII clock bring-up. PHY identifiers and link have been observed on hardware; full TX/RX packet-pin acceptance remains in the MAC/DMA milestone.
2. Read-only link-state handling and its documented BMSR latch sequence are implemented.
   [PHY reset/write/autonegotiation control](phy-control.md) is implemented and host-tested with bounded deadlines; [selected hardware acceptance](phy-control-hardware-2026-11-14.json) passed, including cable loss, late insertion and standalone cold start.
3. Verify the configured RMII TX/RX pins on hardware (MDIO/clock bring-up alone does not prove the packet interface). Connect the host-tested ring state model to real DMA descriptors and D2-accessible buffers; specify cache-line alignment,
   barriers and cache/MPU strategy; test ring wrap, starvation and error recovery before live traffic.
4. Pin and audit upstream lwIP, add a `NO_SYS=1` port, one main-loop owner, static pools, and a
   bounded netif adapter. `STABLE-2_2_1_RELEASE` is a candidate tag, not yet an integrated dependency.
5. Validate external ARP/ping, then bounded UDP/TCP behavior, load, cable loss/reconnect and reset.
   E2E must observe actual external traffic/results from the exact identified firmware image.

For this project's next Ethernet milestone, complete PR checks and merge first,
then build a traceable immutable firmware artifact from the merged `main` commit.
Download and verify that artifact before flashing the board; perform Flash readback
and MAC/DMA HIL against those exact bytes. The artifact job must complete and its
pinned toolchain must be verified before the next MAC/DMA image is flashable; see
[artifact procedure](firmware-artifacts.md). Pre-merge programming is reserved for a
separately authorized, backed-up experiment and never counts as release deployment.

Public source publication and hardware flashing are separate authorized operations.
Host CI proves neither target compilation nor hardware operation. See the dated hardware
report and link/coverage guide for local evidence; MAC/DMA, fuzzing and full-network HIL
remain separate gates.

## Primary references

- [UM2408 Rev 6, section 7.12 and Table 14, pp. 31–32](https://www.st.com/resource/en/user_manual/dm00499171-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf):
  LAN8742A RMII, JP6/JP7, and board-specific pin mapping. Physical configuration still needs verification.
- [RM0399](https://www.st.com/resource/en/reference_manual/dm00176879-stm32h745-755-and-stm32h747-757-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf):
  Ethernet MAC MDIO address/data registers; use the exact current revision for implementation review.
- [LAN8742A DS00001989A, sections 4.2.3–4.2.4](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf): PHY identifier registers.
- [lwIP NO_SYS](https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__nosys.html): callback raw API and single-context access.

Book navigation: Making Embedded Systems, second edition, chapter 3 (bring-up, interfaces,
error handling), chapter 5 (interrupts), chapter 7 (peripherals/DMA), and chapter 10 (connected
devices). The user-provided book is not included in this repository.
