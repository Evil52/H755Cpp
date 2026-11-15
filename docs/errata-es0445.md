# STM32H755 errata assessment

Source: ST ES0445 Rev 7, 30 October 2026, 60 pages. The local reference copy is
`.reference/ES0445-Rev7.pdf` (SHA-256
`ebb7d6328b42ce517908ea2550fabf777734edefa5983467033d4b0038f6e0f5`).
The PDF is ignored by Git; use the [official ST document](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)
to check for later revisions.

The SWD readback `DBGMCU_IDC = 0x20036450` gives `REV_ID = 0x2003`.
ES0445 Table 2 maps that value to silicon revisions U/V. The package marking
has not been read, so this record uses the register ID for applicability.
Rev 7 also lists `REV_ID = 0x2007` for other markings; those are not the
observed silicon. Its revision history does not list a new ETH erratum.

## Current Ethernet design

| ES0445 section | Limitation | Current assessment |
| --- | --- | --- |
| 2.3.11 | MII unavailable on some packages | The board uses RMII. |
| 2.25.1, 2.25.4 | Weighted round-robin Tx/Rx arbitration can violate priority | The driver does not program a weighted ratio or rely on a QoS guarantee. Throughput under load still needs HIL. |
| 2.25.2, 2.25.3 | Recovery after DMA bus error can fail in timestamping/TSO cases | The driver enables neither Rx timestamping nor TSO/USO. A fatal bus error latches a fault; it does not silently restart DMA. |
| 2.25.5, 2.25.6, 2.25.7, 2.25.9, 2.25.11, 2.25.12 | Filtering, timestamp/PPS, MAC-generated packet, ARP offload and TSO limitations | The current firmware uses software ARP in lwIP and does not enable these hardware functions. Reassess if any is enabled. |
| 2.25.8 | Spurious receive watchdog timeout interrupt for specific register values | The driver does not enable ETH DMA interrupts or program the receive watchdog timeout register. |
| 2.25.10 | Valid RMII 10 Mbit/s frames may be dropped with fake dribble and CRC flags | The PHY and MAC currently allow 10 Mbit/s; the driver does not set `ETH_MTLRXQOMR.FEP`. The documented workaround accepts error-flagged frames, which needs a security and packet-integrity review before enabling. Until then, 10 Mbit/s delivery is an open acceptance gap. Test 100 and 10 Mbit/s separately on the target. |
| 2.1.1, 2.1.5 | M7 D-cache write-through and invalidate ordering limitations | MAC startup refuses enabled D-cache. Reassess with the exact MPU/cache design before enabling it. |

This is a source-to-code review, not silicon verification. Isolated 100 Mbit/s
traffic passed for one temporary candidate image; 10 Mbit/s and exact-image
Release acceptance remain open.
