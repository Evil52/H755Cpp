# Pinned CMSIS inputs

- CMSIS-Core 6.4.0: Arm repository commit `af1b110e46a1720349f6f125e386e126767699bd`.
  Source: https://github.com/ARM-software/CMSIS_6/tree/af1b110e46a1720349f6f125e386e126767699bd
  Selected Core Include files; license: `cmsis-core/LICENSE`.
- ST CMSIS-Device H7 v1.10.8: commit `277b283ad9ab79ea85ee42920e371b019d7defb0`.
  Source: https://github.com/STMicroelectronics/cmsis-device-h7/tree/277b283ad9ab79ea85ee42920e371b019d7defb0
  Selected device-selection, H755, system declaration headers, H755 GCC startup and dual-core system source; license: `cmsis-device-h7/LICENSE.md`.

Files are unmodified. `manifest.json` records per-file SHA256 and source commit.
No HAL/LL or RTOS implementation is included. Current target checks compile the
CMSIS MDIO adapter; they do not establish physical peripheral behavior.

Upstream whitespace is preserved to retain exact source hashes. `.gitattributes`
exempts only these unmodified vendor trees from whitespace diagnostics; owned code,
the manifest and this document remain checked.

## USB async subset

`usb-async-manifest.json` records full upstream commits and original file SHA256 values
for TinyUSB0.21.0, Intel async's cpp20 branch, baremetal-concurrency, stdx and Boost.MP11.
Only needed TinyUSB core/common/OS-none/CDC/DWC2 directories are copied; no HAL BSP,
USB host, HID host, MTP or runtime dependency download is built. Header-only dependencies
are wired with local interface targets rather than running upstream CPM/network setup.
The used stdx subset needs no fmt runtime; fmt-dependent APIs are outside this subset.

Local, separately reviewable patches and patched-file hashes are recorded in that manifest:
- `tinyusb-h755-bounded-waits.patch`: finite FS reset/FIFO/endpoint waits and four-packet RX budget.
- `stdx-gcc16-compat.patch`: delay argument-pack indexing until an argument is actually requested;
  zero-argument completion signatures otherwise fail under GCC16 C++26 pack indexing.
- `async-gcc16-compat.patch`: inline header lambda constants to give repeat operation types consistent
  linkage across translation units, rather than silencing GCC subobject-linkage diagnostics.

`SIMULATE_FREESTANDING` is the pinned concurrency/async library's provided selection hook.
An explicit injected single-owner main-loop policy is installed before async headers;
no hosted mutex, exception or RTTI policy is enabled to make the build pass. Only the
selected operations are validated on Arm GCC16.2 and host compilers; the upstream library
labels itself work in progress and this project does not claim universal API qualification.

The published TinyUSB HID-host parser advisory/fix (85047e1) concerns an unvendored,
uncompiled host class. The reported MTP string issue/fix (e85c2b8) concerns an unvendored,
uncompiled MTP class. Their non-applicability is limited to this CDC-only device subset;
it is not a blanket statement that TinyUSB0.21.0 has no advisories or vulnerabilities.
Adding classes/host support requires a fresh dependency and applicability review.
References: https://github.com/hathach/tinyusb/commit/85047e1777f5c20b7a936aa905c746125d3ed066
and https://github.com/hathach/tinyusb/commit/e85c2b8affa1efbf00d398d59e7a87d3e61cc4a9 .

CMake verifies every vendored file against its original or explicitly patched SHA at configure time; checksum failures stop the build before compilation.

Patch files use zero-context unified diffs to avoid syntactic context-line trailing spaces; validate/apply against the exact pinned upstream with `git apply --unidiff-zero`. Unused RTOS OSAL headers are not vendored.
