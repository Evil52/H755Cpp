# First hardware bring-up

Scope: bare-metal M7 heartbeat on LD1 (PB0) and read-only LAN8742A PHY identification
through the existing MDIO reader. No MAC/DMA traffic, lwIP, IP address, ping or webserver.
Board source: [UM2408 Rev 6](https://www.st.com/resource/en/user_manual/dm00499171-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf),
sections 7.4.8, 7.6 and 7.12. Default MB1363 direct-SMPS supply and PB0 LED routing are
assumed; Ethernet requires JP6/JP7 ON and its standard solder bridges. Physical PCB
revision has not been read from its marking.

## Startup and ownership

The pinned ST H755 assembly startup calls ExitRun0Mode, SystemInit, initializes data/bss,
constructors and main. Vendor files are unchanged and hashed in vendor/manifest.json.
USE_PWR_DIRECT_SMPS_SUPPLY selects the board's default supply. HSI is retained at 64 MHz;
main verifies the computed core/HCLK values. No PLL, VOS increase, cache or DMA is enabled.
The vendor power-readiness wait is unbounded; this bring-up is not a production boot
recovery implementation. STM32H74x/75x Rev V was identified through SWD (DBGMCU 0x20036450).
SWD identifies the device family, not the physical board SKU by itself.

The existing boot configuration starts both cores, M7 at 0x08000000 and M4 at 0x08100000.
M7 owns GPIO/RCC/Ethernet. A separate 92-byte assembly-only M4 parking image disables
interrupts, writes PARK to SRAM4 0x38000000 and waits in WFE. It links no C++ runtime,
M7 libraries or FPU operations. Its private initial stack is 0x38010000. There is no
M4 application or general multicore IPC protocol; the marker alone is not a liveness proof.
M7 caches remain off. Both images must be programmed as a pair; option bytes are unchanged.

M7 links only within bank 1 sector 0 (128 KiB) and AXI SRAM; initial stack 0x24080000,
8 KiB reserved by linker assertion. M4 occupies bank 2 sector 0. No heap use is linked.
Linker scripts retain vectors and initialization arrays. All generated ELF/BIN/HEX/MAP
and .su files live under ignored build directories. Builds never write hardware.

## Build and acceptance

```sh
export H755_ARM_TOOLCHAIN=/path/to/arm-gcc-16.2.0-cm7
cmake --preset cm7-debug
cmake --build --preset cm7-debug
cmake --preset cm7-release
cmake --build --preset cm7-release
```

Outputs are firmware/cm7_bringup.{elf,bin,hex,map} and firmware/cm4_park.{elf,bin,hex,map}
inside the corresponding build directory. Flash only identified firmware artifacts.
The toolchain-checks/link_only.elf remains a non-bootable runtime link probe.

M7 scans PHY addresses 0..31, with 20 ms per MDIO transaction, after a 200 ms PHY startup
delay. It reads registers 2/3 and matches the revision-masked LAN8742A identifier.
SysTick is the single-writer millisecond source; foreground reads a naturally aligned
32-bit volatile snapshot. No threads or other-core access to it are supported. Unsigned
elapsed-time arithmetic intentionally supports wrap. SysTick failure is not independently
watched in this bring-up. Reads can fault-latch and report MDIO errors; timeout is not
cancellation of the hardware request. No PHY writes are issued.

Debugger symbol g_bringup has ten 32-bit words: magic, state, heartbeat, HCLK Hz,
PHY address, ID1, ID2, MDIO errors, fault code, observed M4 marker. State 4 means matched,
5 means no match; 0xffffffff means stopped. Green toggles every 500 ms, red is off on
match and on on failure. Contract/assert/fault stops disable interrupts, turn green off
and red on. This LED-only fault action is not validated for other output devices.
GNU ld wraps the pinned GCC terminate/assert entry points into the local fault action;
the application does not define replacement functions inside namespace std. The native
replaceable contract violation handler is defined at global scope. Verify the wrapped
symbols when changing compiler/runtime versions.

A pre(pin < 16U) on the GPIO adapter caused a GCC 16.2 internal compiler error during
code generation. The same invariant uses native contract_assert in its body instead,
with -fcontracts -fcontract-evaluation-semantic=enforce retained in Debug and Release.
Do not infer general native-contract support from the earlier small probes. Controlled
violation handling and physical LED visibility remain separate acceptance evidence.

Host tests and Debug/Release builds do not establish hardware acceptance. Record exact
image hashes, flash verification, unchanged option bytes/unaffected sectors, observed
PHY IDs, advancing heartbeat and M4 PC in the hardware report after programming.

## Recorded hardware result

[2026-11-14 report](hardware-2026-11-14.json): Release source commit 6751b5f was
programmed and verified on the connected board. Both binaries match the full Flash
readback, sectors 1..7 in both banks and option/boot configuration are unchanged.
PHY address 0 returned 0x0007/0xC131, state 4, with zero MDIO errors/faults. Heartbeat
advanced 69 to 70 and GPIOB ODR changed 1 to 0; M4 PC was 0x08100050 in Park_Idle.
These are debugger observations; optical LED confirmation and power-cycle behavior
remain separate. Flash backups and full logs stay local under ignored build/hardware.

Validation: three CTest groups under host ASan/UBSan, five Python reporting tests,
Debug/Release target builds, vendor SHA256 verification, Semgrep project rules on five
owned files including firmware/cm7/main.cpp, and Gitleaks passed. Target firmware
was not analyzed by host-only CodeQL/Clang, and CI for these new commits has not run.
