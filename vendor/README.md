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
