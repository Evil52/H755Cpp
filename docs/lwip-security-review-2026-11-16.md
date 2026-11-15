# lwIP security review — 16 November 2026

## Exact scope

The project pins upstream lwIP tag `STABLE-2_2_1_RELEASE`, commit
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`, with per-file hashes in
`vendor/lwip-manifest.json`. `cmake/lwip.cmake` compiles the IPv4, ARP, ICMP,
DHCP client, UDP and TCP paths. It does not compile the lwIP SMTP, DNS,
HTTP, MQTT or SNMP applications or a DHCP server. `include/lwipopts.h`
disables IPv6, forwarding, IP reassembly, sockets and netconn.

| Finding/source | Applicability to this image | Disposition |
| --- | --- | --- |
| [Upstream ARP short-packet issue #108](https://github.com/lwip-tcpip/lwip/pull/108), still open at review time | ARP is compiled. The upstream parser can read beyond a short ARP pbuf. | `LwipNetwork::receive` rejects Ethernet/ARP frames shorter than 42 bytes before calling lwIP. The real lwIP host integration test exercises a short ARP frame under ASan/UBSan. Preserve this boundary when changing RX paths; an upstream fix remains preferable. |
| [xchglabs 2026 lwIP audit](https://github.com/xchglabs/lwip-2026-pocs) | The public SMTP finding is outside this image. The researchers report twelve further, not yet disclosed findings across DNS, DHCP, HTTP/MQTT/SNMP and core IP; DHCP and core IP are compiled here. | Scope and severity of the undisclosed DHCP/IP findings cannot yet be established. Keep network-exposed Release security acceptance open. CI, Sonar and the 512 malformed-frame host corpus do not resolve an undisclosed issue. |
| [GitHub CodeQL alert #1](https://github.com/Evil52/H755Cpp/security/code-scanning/1) | Medium-severity `actions/untrusted-checkout` finding in the artifact workflow, outside lwIP. The workflow currently triggers only on successful `push` CI runs for `main` and uses read-only permissions. | Review release workflow trust assumptions separately. Do not silently close the alert or count it as a lwIP finding. The Code Scanning API returned no other open alerts at review time. |

This is an applicability review, not a claim that the network parser is free of
vulnerabilities. The candidate has only been exercised on a direct isolated
Ethernet link. Do not connect it to an untrusted LAN or call it production-ready
while the applicable undisclosed findings remain unresolved.

## Release decision and recheck

Keep PR #14 as a candidate until the DHCP/core-IP findings are disclosed or an
independent review establishes that the compiled paths are unaffected. For each
applicable finding, pin the upstream correction or a reviewed local patch,
retain a regression input, run the real lwIP host path under ASan/UBSan, rebuild
both M7 profiles, repeat coverage and local Sonar, and recheck the exact
post-merge Release artifact on the board. Revisit this review whenever the
lwIP pin, enabled source list, RX adapter, or upstream disclosures change.
