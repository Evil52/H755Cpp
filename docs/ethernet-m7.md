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
   [PHY reset/write/autonegotiation control](phy-control.md) is implemented and host-tested with bounded deadlines; new physical acceptance remains pending.
3. Complete and verify the RMII TX/RX pin configuration (MDIO/clock bring-up alone does not prove the packet interface). Specify DMA descriptor ownership and bounded rings, D2-accessible buffers, cache-line alignment,
   barriers and cache/MPU strategy; test ring wrap, starvation and error recovery before live traffic.
4. Pin and audit upstream lwIP, add a `NO_SYS=1` port, one main-loop owner, static pools, and a
   bounded netif adapter. `STABLE-2_2_1_RELEASE` is a candidate tag, not yet an integrated dependency.
5. Validate external ARP/ping, then bounded UDP/TCP behavior, load, cable loss/reconnect and reset.
   E2E must observe actual external traffic/results from the exact identified firmware image.

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
