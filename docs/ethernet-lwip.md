# M7 Ethernet protocol candidate

This is an unmerged candidate, not a deployed Release. The post-merge `cm7-ethernet-release`
artifact and exact-image board acceptance are required before deployment.

## Scope and ownership

- M7 main loop owns the CMSIS MAC/DMA driver, lwIP raw API, DHCP state and echo
  endpoints. No Ethernet ISR, RTOS task or M4 code calls lwIP. The synchronous RX
  callback copies the borrowed DMA frame into a lwIP pbuf before the descriptor
  returns to the ring. TX copies the pbuf chain into one bounded MAC frame.
- The vendored upstream lwIP revision and individual source hashes are pinned in
  `vendor/lwip-manifest.json`; CMake checks those hashes. The C implementation is
  kept separate from owned C++ adapters and is excluded from owned-code coverage.
- `NO_SYS=1` enables only the raw API. IPv4, DHCP/ACD, ARP, ICMP, UDP and TCP are
  compiled; DNS, sockets, netconn, IPv6, IP fragmentation/reassembly, forwarding
  and lwIP applications are disabled. The source inventory in `cmake/lwip.cmake`
  is the compiled attack surface, not the whole vendor tree.
- lwIP uses fixed heap/pools and an 8-byte minimum alignment. MAC RX/TX frames
  are bounded at 1536 bytes, UDP echo payload at 512 bytes, and TCP echo keeps
  one connection with a 536-byte MSS. Pool exhaustion or a full MAC TX ring
  returns an error; it must not be silently interpreted as successful delivery.
- `sys_now` receives the existing M7 monotonic millisecond tick. The target RNG
  uses the STM32 hardware RNG with bounded HSI48/RNG readiness checks and enters
  the Ethernet fault path on a hardware error. RNG clocking, USB coexistence and
  cold-start behavior remain target HIL requirements.
- `g_network` exposes DHCP readiness, IPv4 address, RX rejects and TX counters;
  `g_echo` exposes UDP/TCP echoes and rejects for read-only SWD observation. These
  diagnostics are not a network management interface.

## Current evidence and acceptance

Host integration runs the real pinned lwIP with a fake MAC under ASan/UBSan. It
checks DHCP discover/offer/acknowledgement and ACD, ARP reply, ICMP echo, UDP and
TCP echo, oversized UDP rejection, TX backpressure, short Ethernet/ARP/IPv4 frame rejection, and
link loss/recovery. This does not establish real RMII traffic or timing.

An isolated, temporary HIL on candidate commit `96a1560` and instrumented BIN
SHA-256 `3a3adc6b84fc25e3a222240d07c7fb88f813333c7c45ffb78cc7e30b95610390`
passed direct Mac-to-board DHCP/ACD (`192.168.77.2`), three ICMP replies, two
UDP echoes and two TCP echoes. Read-only SWD diagnostics reported MAC running,
24 RX frames, 20 TX frames, zero MAC errors or drops, and zero echo rejects.
The same isolated scenarios passed again for the updated source commit
`36cefb4`, after all eight PR CI checks passed (run `43748095333`). Its
instrumented BIN SHA-256 was
`ca052df6d2348875a7f9c0ef4ee7a377b6e96ab4ae0eae6f4e3df1597d519c77`.
The M7 coverage stream was 12,827 bytes with no collection error; fresh host,
target and Python results gave 88.92% whole-owned-code line coverage with no
unmeasured source. Local Sonar passed with zero issues and 88.9% coverage.
The original full Flash SHA-256 `6062248b5b56ad4929cfc3fa70a91e9a51c2f681b8bb77b7838a40af1ebc6d14`
matched after both temporary runs. The Mac's test DHCP process and address
were removed afterward. These results prove only the exercised scenarios for
the named instrumented images; the post-merge Release needs its own acceptance.

A later isolated run of the same instrumented BIN, after the green CI run for
documentation commit `7d59fe0`, passed 256 sequential UDP echoes and 16 TCP
connections with exact payload matches in 0.42 s. This exercised repeated
descriptor reuse, but is not a throughput or worst-case latency claim. The
reusable host probe is `tests/hil/lwip_load_probe.py`. A planned cable-loss
check did not execute because the cable remained connected; no link recovery
claim follows from this run. The temporary image was restored, and the full
Flash readback again matched the preflight SHA-256 above. The Mac's temporary
DHCP process and test address were removed.

On 16 November 2026 a later link-recovery HIL stopped before any Flash write:
OpenOCD could read the ST-LINK and SWD DPIDR but reported Cortex-M `PARTNO 0x0`
for both cores, even at 50 kHz and after one manual board RESET. STM32CubeProgrammer
v2.20.0 in HOTPLUG mode likewise could not identify the device. One separately
authorized connect-under-reset read identified NUCLEO-H755ZI-Q, device `0x450`,
revision V, `DBGMCU_IDC 0x20036450`; normal SWD access still failed afterward.
The user observed the normal green heartbeat and red LED off. The test DHCP/IP
were removed. The cause of running-target SWD failure remains unknown, and no
link-loss or recovery evidence was collected in this attempt. Diagnose debug
access before another candidate flash; do not treat under-reset identification
as proof of normal live-debug access.

The adapter rejects ARP frames shorter than 42 bytes before the pinned lwIP
`etharp_input()` path. This is a local mitigation for the [publicly reported
short-ARP out-of-bounds read](https://github.com/lwip-tcpip/lwip/pull/108); it
does not establish that other lwIP parser vulnerabilities are absent. The
[2026 upstream audit](https://github.com/xchglabs/lwip-2026-pocs) also reports
unresolved findings involving DHCP and the core IP stack, whose details are not
yet public. Treat the network-exposed Release security review as open.

CodeQL reported `cpp/comparison-with-wider-type` at unchanged upstream
`src/core/mem.c:857`. In this build `MEM_SIZE` is 16,384 and alignment is 8,
so `MEM_SIZE_ALIGNED` is 16,384. `mem_size_t` is 16-bit because upstream selects
it whenever `MEM_SIZE <= 64,000`. The preceding guard returns if `size` exceeds
`MEM_SIZE_ALIGNED`, hence the loop bound `MEM_SIZE_ALIGNED - size` is in
`[0, 16384]`, representable in `mem_size_t`; this particular width-comparison
warning is not a reachable overflow in our configuration. This assessment is
specific to the pinned options and does not waive other vendor findings.

Before normal board deployment, the exact committed candidate needs green PR checks,
fresh whole-owned-code target coverage above 80%, local Sonar gate, and the
project's post-merge artifact path. An isolated temporary coverage HIL requires
its own CI-green candidate and backup/restoration procedure. The delivered
Release artifact then needs Flash readback and external-host checks for DHCP,
ARP, ping, UDP/TCP echo, link loss/recovery, cold power-on, ring wrap, pressure,
USB coexistence, and fault/timeout behavior. Record the exact image hash and
observed limits. Do not call a successful host simulation production acceptance.
