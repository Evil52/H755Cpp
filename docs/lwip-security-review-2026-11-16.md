# lwIP security review — 16 November 2026

## Exact scope

The project bases lwIP on upstream tag `STABLE-2_2_1_RELEASE`, commit
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`, with per-file hashes and the
one local ARP backport recorded in
`vendor/lwip-manifest.json`. `cmake/lwip.cmake` compiles the IPv4, ARP, ICMP,
DHCP client, UDP and TCP paths. It does not compile the lwIP SMTP, DNS,
HTTP, MQTT or SNMP applications or a DHCP server. `include/lwipopts.h`
disables IPv6, forwarding, IP reassembly, sockets and netconn.

| Finding/source | Applicability to this image | Disposition |
| --- | --- | --- |
| [Upstream ARP short-packet issue #108](https://github.com/lwip-tcpip/lwip/pull/108), still open at review time | ARP is compiled. The upstream parser can read beyond a short first pbuf. | `LwipNetwork::receive` rejects Ethernet/ARP frames shorter than 42 bytes. A local backport additionally checks `p->len` before `etharp_input()` reads the header. The direct host regression uses a guarded one-byte `PBUF_REF`: it failed at `etharp_input()` before the patch and passes afterward. Keep both checks until an upstream release is assessed. |
| [xchglabs 2026 lwIP audit](https://github.com/xchglabs/lwip-2026-pocs) | The public SMTP finding is outside this image. The researchers report twelve further, not yet disclosed findings across DNS, DHCP, HTTP/MQTT/SNMP and core IP; DHCP and core IP are compiled here. | Scope and severity of the undisclosed DHCP/IP findings cannot yet be established. Keep network-exposed Release security acceptance open. CI, Sonar and the 512 malformed-frame host corpus do not resolve an undisclosed issue. |
| [GitHub CodeQL alert #1](https://github.com/Evil52/H755Cpp/security/code-scanning/1) | Medium-severity `actions/untrusted-checkout` finding in the artifact workflow, outside lwIP. | PR #15 merged as `fd046f0`; CodeQL on `main` marked the alert fixed, and the post-merge artifact workflow succeeded. No manual dismissal was used. |

This is an applicability review, not a claim that the network parser is free of
vulnerabilities. The candidate has only been exercised on a direct isolated
Ethernet link. Do not connect it to an untrusted LAN or call it production-ready
while the applicable undisclosed findings remain unresolved.

## Limited board deployment decision and recheck

On 16 November 2026 the user explicitly accepted the unresolved applicability
and severity of the undisclosed DHCP/core-IP findings for this development
board, authorized merging PR #14, and authorized flashing only its verified
post-merge artifact after successful CI/CD. This is a documented exception for
evaluation on the isolated direct Mac Ethernet link, not a statement that the
stack is secure or ready for an untrusted LAN or production deployment. The
network-exposed Release security acceptance remains open.

For each applicable finding, pin the upstream correction or a reviewed local patch,
retain a regression input, run the real lwIP host path under ASan/UBSan, rebuild
both M7 profiles, repeat coverage and local Sonar, and recheck the exact
post-merge Release artifact on the board. Revisit this review whenever the
lwIP pin, enabled source list, RX adapter, or upstream disclosures change.
