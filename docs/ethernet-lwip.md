# M7 Ethernet protocol candidate

This is an unmerged, unflashed candidate. The post-merge `cm7-ethernet-release`
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

The adapter rejects ARP frames shorter than 42 bytes before the pinned lwIP
`etharp_input()` path. This is a local mitigation for the [publicly reported
short-ARP out-of-bounds read](https://github.com/lwip-tcpip/lwip/pull/108); it
does not establish that other lwIP parser vulnerabilities are absent. The
[2026 upstream audit](https://github.com/xchglabs/lwip-2026-pocs) also reports
unresolved findings involving DHCP and the core IP stack, whose details are not
yet public. Treat the network-exposed Release security review as open.

Before board programming, the exact committed candidate needs green PR checks,
fresh whole-owned-code target coverage above 80%, local Sonar gate, and the
project's post-merge artifact path. An isolated temporary coverage HIL requires
its own CI-green candidate and backup/restoration procedure. The delivered
Release artifact then needs Flash readback and external-host checks for DHCP,
ARP, ping, UDP/TCP echo, link loss/recovery, cold power-on, ring wrap, pressure,
USB coexistence, and fault/timeout behavior. Record the exact image hash and
observed limits. Do not call a successful host simulation production acceptance.
