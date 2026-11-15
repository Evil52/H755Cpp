# Firmware artifact after merge

Normal H755 deployment uses the exact image produced by `Firmware artifact` after a
successful `CI` push run on a two-parent merge commit in `main`. The artifact workflow
checks that the merge corresponds to a PR whose seven prerequisite jobs and final
`Quality gate` job passed on the
exact merged head. It does not rerun host tests, Semgrep or CodeQL. The main `CI` run
contains the already configured CodeQL push checks.

The artifact job runs on the standard GitHub-hosted `macos-15` arm64 runner. It downloads
the separately packaged, SHA-256-pinned GCC 16.2 CM7 toolchain from the repository's
toolchain release, verifies the archive and compiler binary, and builds the explicit
`cm7-ethernet-release` preset. The binary includes the M7 C++ runtime; Homebrew's standalone
cross-compiler package is not a substitute. The archive is a build dependency, never
committed to the source repository. The release notes and source tarballs accompany it.

`tools/firmware_artifact.py` copies M7/M4 ELF, BIN, HEX and map files into one artifact.
`manifest.json` records the merged commit/tree, preset, compiler digest, PR/main CI run
IDs, intended load addresses, file sizes and SHA-256 hashes. The artifact upload is
immutable within its Actions run and retained for 90 days. This is a **HIL candidate**,
not an accepted production release. After exact-image HIL, promote the same verified
bytes to a longer-lived signed release with a separate approval and retention policy.

Before programming, download the artifact for the intended merge SHA and verify locally:

```sh
gh run download ARTIFACT_WORKFLOW_RUN_ID --repo Evil52/H755Cpp \
  --name h755-firmware-MERGE_SHA --dir /path/to/downloaded-artifact
python3 tools/firmware_artifact.py verify \
  --bundle /path/to/downloaded-artifact \
  --expected-commit MERGE_SHA --expected-preset cm7-ethernet-release
```

Check the workflow run's successful conclusion, manifest CI run IDs, source commit,
board/core/load addresses and compiler hash before flashing. Keep a full board Flash
backup and a known-good rollback image. Program only the reviewed core/sector from the
downloaded artifact, verify Flash readback, and run hardware tests on those bytes.
No workflow here flashes hardware automatically. M4 and option bytes remain outside a
normal M7 update unless a separately reviewed release explicitly changes them.

The toolchain release and artifact workflow must be tested green before the next
Ethernet candidate is flashed. A successful PR alone, a local `build/` image, a skipped
artifact job or a draft release with unverified toolchain access does not pass this gate.
The GitHub Actions artifact expires; archive approved production releases separately.
