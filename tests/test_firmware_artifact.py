import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools/firmware_artifact.py"
spec = importlib.util.spec_from_file_location("firmware_artifact", MODULE_PATH)
artifact = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifact)


class FirmwareArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.commit = "a" * 40
        self.manifest = {
            "schema": 1,
            "kind": "post_merge_hil_candidate",
            "source_commit": self.commit,
            "source_tree": "b" * 40,
            "build_preset": "cm7-ethernet-release",
            "compiler_sha256": "c" * 64,
            "main_ci_run_id": 42,
            "pr_ci_run_id": 41,
            "load_addresses": dict(artifact.ADDRESSES),
            "files": {},
        }
        for name in artifact.FILES:
            path = self.root / name
            path.write_bytes(name.encode())
            self.manifest["files"][name] = {
                "sha256": artifact.digest(path), "bytes": path.stat().st_size,
            }
        self.save()

    def save(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def test_exact_bundle_passes(self):
        artifact.verify(self.root, self.commit, "cm7-ethernet-release")

    def test_changed_firmware_or_commit_is_rejected(self):
        (self.root / "cm7_bringup.bin").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "content mismatch"):
            artifact.verify(self.root, self.commit, "cm7-ethernet-release")
        with self.assertRaisesRegex(ValueError, "source commit"):
            artifact.verify(self.root, "d" * 40, "cm7-ethernet-release")

    def test_wrong_profile_and_address_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "release preset"):
            artifact.verify(self.root, self.commit, "cm7-release")
        self.manifest["load_addresses"]["cm4_park.bin"] = "0x08000000"
        self.save()
        with self.assertRaisesRegex(ValueError, "load-address"):
            artifact.verify(self.root, self.commit, "cm7-ethernet-release")

    def test_symlinked_image_is_rejected(self):
        target = self.root / "cm4_park.bin"
        target.unlink()
        target.symlink_to(self.root / "cm7_bringup.bin")
        with self.assertRaisesRegex(ValueError, "content mismatch"):
            artifact.verify(self.root, self.commit, "cm7-ethernet-release")


class FirmwareArtifactCreationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.build = self.root / "build"
        self.toolchain = self.root / "toolchain"
        self.output = self.root / "artifact"
        self.commit = "a" * 40
        self.tree = "b" * 40
        (self.source / "docs").mkdir(parents=True)
        (self.build / "firmware").mkdir(parents=True)
        (self.build / "CMakeFiles/3.31.6").mkdir(parents=True)
        (self.toolchain / "bin").mkdir(parents=True)
        self.compiler = self.toolchain / "bin/arm-none-eabi-g++"
        self.compiler.write_bytes(b"pinned compiler fixture")
        (self.source / "docs/toolchain-validation.json").write_text(json.dumps({
            "compiler_sha256": artifact.digest(self.compiler),
        }))
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n"
            "CMAKE_BUILD_TYPE:STRING=Release\nH755_USB_CDC:BOOL=ON\n"
            "H755_ETHERNET_MAC:BOOL=ON\nH755_TARGET_COVERAGE:BOOL=OFF\n"
            "H755_ETHERNET_DIAGNOSTIC:BOOL=OFF\n")
        (self.build / "CMakeFiles/3.31.6/CMakeCXXCompiler.cmake").write_text(
            f'set(CMAKE_CXX_COMPILER "{self.compiler}")\n')
        for name in artifact.FILES:
            (self.build / "firmware" / name).write_bytes(name.encode())

    def git_output(self, command, text):
        self.assertTrue(text)
        if command[-1] == "HEAD":
            return self.commit + "\n"
        if command[-1] == "HEAD^{tree}":
            return self.tree + "\n"
        if command[-1] == "--porcelain":
            return ""
        self.fail(f"unexpected git command: {command}")

    def create(self, expected_commit=None):
        with mock.patch.object(artifact.subprocess, "check_output", side_effect=self.git_output):
            artifact.create(self.source, self.build, self.output, "cm7-ethernet-release",
                            "42", "41", self.toolchain, expected_commit or self.commit)

    def test_creates_manifest_from_exact_clean_build(self):
        self.create()
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual((manifest["source_commit"], manifest["source_tree"]),
                         (self.commit, self.tree))
        self.assertEqual((manifest["main_ci_run_id"], manifest["pr_ci_run_id"]),
                         (42, 41))
        artifact.verify(self.output, self.commit, "cm7-ethernet-release")

    def test_rejects_disabled_ethernet_in_release_cache(self):
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n"
            "CMAKE_BUILD_TYPE:STRING=Release\nH755_USB_CDC:BOOL=ON\n"
            "H755_ETHERNET_MAC:BOOL=OFF\n")
        with self.assertRaisesRegex(ValueError, "Ethernet build selection"):
            self.create()

    def test_rejects_instrumented_or_diagnostic_release(self):
        cache = self.build / "CMakeCache.txt"
        original = cache.read_text()
        cache.write_text(original.replace("H755_TARGET_COVERAGE:BOOL=OFF",
                                          "H755_TARGET_COVERAGE:BOOL=ON"))
        with self.assertRaisesRegex(ValueError, "Coverage build"):
            self.create()
        cache.write_text(original.replace("H755_ETHERNET_DIAGNOSTIC:BOOL=OFF",
                                          "H755_ETHERNET_DIAGNOSTIC:BOOL=ON"))
        with self.assertRaisesRegex(ValueError, "Diagnostic build"):
            self.create()

    def test_rejects_wrong_commit_compiler_and_stale_build(self):
        with self.assertRaisesRegex(ValueError, "checkout"):
            self.create("c" * 40)
        self.compiler.write_bytes(b"untrusted compiler")
        with self.assertRaisesRegex(ValueError, "compiler binary"):
            self.create()
        self.compiler.write_bytes(b"pinned compiler fixture")
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_HOME_DIRECTORY:INTERNAL=/wrong/source\nCMAKE_BUILD_TYPE:STRING=Release\n")
        with self.assertRaisesRegex(ValueError, "another source"):
            self.create()


if __name__ == "__main__":
    unittest.main()
