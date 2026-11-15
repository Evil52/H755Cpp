#!/usr/bin/env python3
"""Package and verify exact H755 firmware bytes for a CI release artifact."""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

CM7_BIN = "cm7_bringup.bin"
CM4_BIN = "cm4_park.bin"
MANIFEST_NAME = "manifest.json"
FILES = (
    "cm7_bringup.elf", CM7_BIN, "cm7_bringup.hex", "cm7_bringup.map",
    "cm4_park.elf", CM4_BIN, "cm4_park.hex", "cm4_park.map",
)
ADDRESSES = {CM7_BIN: "0x08000000", CM4_BIN: "0x08100000"}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_cache(path: Path) -> dict[str, str]:
    entries = {}
    for line in path.read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            entries[key.split(":", 1)[0]] = value
    return entries


def validated_checkout(source: Path, expected_commit: str) -> str:
    commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if commit != expected_commit or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("checkout does not match successful main CI commit")
    if subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
        raise ValueError("source tree has uncommitted changes")
    return commit


def validated_build(source: Path, build: Path, preset: str, compiler: Path) -> Path:
    if preset not in ("cm7-release", "cm7-usb-release"):
        raise ValueError("unsupported release preset")
    cache = read_cache(build / "CMakeCache.txt")
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "/missing")).resolve() != source.resolve():
        raise ValueError("build tree belongs to another source checkout")
    compiler_records = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if (cache.get("CMAKE_BUILD_TYPE") != "Release" or len(compiler_records) != 1
            or f'set(CMAKE_CXX_COMPILER "{compiler}")' not in compiler_records[0].read_text()):
        raise ValueError("build configuration or compiler differs from release inputs")
    usb_expected = "ON" if preset == "cm7-usb-release" else "OFF"
    if cache.get("H755_USB_CDC", "OFF") != usb_expected:
        raise ValueError("USB build selection differs from release preset")
    inputs = build / "firmware"
    for name in FILES:
        if not (inputs / name).is_file():
            raise ValueError(f"missing firmware output: {name}")
    if (inputs / CM7_BIN).stat().st_size > 128 * 1024:
        raise ValueError("M7 image exceeds its Flash sector")
    if (inputs / CM4_BIN).stat().st_size > 128 * 1024:
        raise ValueError("M4 image exceeds its Flash sector")
    return inputs


def create(source: Path, build: Path, output: Path, preset: str, main_ci_run: str,
           pr_ci_run: str, toolchain: Path, expected_commit: str) -> None:
    if output.exists():
        raise ValueError(f"artifact output already exists: {output}")
    if not main_ci_run.isdecimal() or int(main_ci_run) <= 0:
        raise ValueError("main CI run ID must be a positive integer")
    if not pr_ci_run.isdecimal() or int(pr_ci_run) <= 0:
        raise ValueError("PR CI run ID must be a positive integer")
    commit = validated_checkout(source, expected_commit)
    expected = json.loads((source / "docs/toolchain-validation.json").read_text())
    compiler = toolchain / "bin/arm-none-eabi-g++"
    if digest(compiler) != expected["compiler_sha256"]:
        raise ValueError("compiler binary differs from pinned GCC 16.2 toolchain")
    inputs = validated_build(source, build, preset, compiler)
    output.mkdir(parents=True)
    records = {}
    for name in FILES:
        shutil.copyfile(inputs / name, output / name)
        records[name] = {"sha256": digest(output / name), "bytes": (output / name).stat().st_size}
    manifest = {
        "schema": 1,
        "kind": "post_merge_hil_candidate",
        "source_commit": commit,
        "source_tree": subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD^{tree}"], text=True).strip(),
        "build_preset": preset,
        "compiler_sha256": digest(compiler),
        "main_ci_run_id": int(main_ci_run),
        "pr_ci_run_id": int(pr_ci_run),
        "load_addresses": dict(ADDRESSES),
        "files": records,
    }
    (output / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    verify(output, commit)


def validate_manifest(manifest: dict, expected_commit: str, expected_preset: str | None) -> None:
    if manifest.get("schema") != 1 or manifest.get("kind") != "post_merge_hil_candidate":
        raise ValueError("unsupported artifact manifest")
    if manifest.get("source_commit") != expected_commit:
        raise ValueError("artifact source commit does not match expected commit")
    if expected_preset is not None and manifest.get("build_preset") != expected_preset:
        raise ValueError("artifact release preset does not match expected preset")
    if not re.fullmatch(r"[0-9a-f]{40}", manifest.get("source_tree", "")):
        raise ValueError("artifact source tree is invalid")
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("compiler_sha256", "")):
        raise ValueError("artifact compiler hash is invalid")
    for key in ("main_ci_run_id", "pr_ci_run_id"):
        if not isinstance(manifest.get(key), int) or manifest[key] <= 0:
            raise ValueError(f"artifact {key} is invalid")
    if manifest.get("load_addresses") != ADDRESSES or set(manifest.get("files", {})) != set(FILES):
        raise ValueError("artifact file or load-address set is invalid")


def verify(bundle: Path, expected_commit: str, expected_preset: str | None = None) -> None:
    if (bundle / MANIFEST_NAME).is_symlink():
        raise ValueError("manifest must be a regular file")
    manifest = json.loads((bundle / MANIFEST_NAME).read_text())
    validate_manifest(manifest, expected_commit, expected_preset)
    for name in FILES:
        record = manifest["files"][name]
        path = bundle / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size != record["bytes"] or digest(path) != record["sha256"]:
            raise ValueError(f"artifact content mismatch: {name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    package = sub.add_parser("create")
    package.add_argument("--source", type=Path, required=True)
    package.add_argument("--build", type=Path, required=True)
    package.add_argument("--output", type=Path, required=True)
    package.add_argument("--preset", required=True)
    package.add_argument("--main-ci-run", required=True)
    package.add_argument("--pr-ci-run", required=True)
    package.add_argument("--toolchain", type=Path, required=True)
    package.add_argument("--expected-commit", required=True)
    check = sub.add_parser("verify")
    check.add_argument("--bundle", type=Path, required=True)
    check.add_argument("--expected-commit", required=True)
    check.add_argument("--expected-preset", required=True)
    args = parser.parse_args()
    if args.command == "create":
        create(args.source, args.build, args.output, args.preset,
               args.main_ci_run, args.pr_ci_run,
               args.toolchain, args.expected_commit)
    else:
        verify(args.bundle, args.expected_commit, args.expected_preset)
    print("Firmware artifact verified")


if __name__ == "__main__":
    main()
