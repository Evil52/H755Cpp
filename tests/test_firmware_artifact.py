import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

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
            "build_preset": "cm7-usb-release",
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
        artifact.verify(self.root, self.commit, "cm7-usb-release")

    def test_changed_firmware_or_commit_is_rejected(self):
        (self.root / "cm7_bringup.bin").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "content mismatch"):
            artifact.verify(self.root, self.commit, "cm7-usb-release")
        with self.assertRaisesRegex(ValueError, "source commit"):
            artifact.verify(self.root, "d" * 40, "cm7-usb-release")

    def test_wrong_profile_and_address_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "release preset"):
            artifact.verify(self.root, self.commit, "cm7-release")
        self.manifest["load_addresses"]["cm4_park.bin"] = "0x08000000"
        self.save()
        with self.assertRaisesRegex(ValueError, "load-address"):
            artifact.verify(self.root, self.commit, "cm7-usb-release")

    def test_symlinked_image_is_rejected(self):
        target = self.root / "cm4_park.bin"
        target.unlink()
        target.symlink_to(self.root / "cm7_bringup.bin")
        with self.assertRaisesRegex(ValueError, "content mismatch"):
            artifact.verify(self.root, self.commit, "cm7-usb-release")


if __name__ == "__main__":
    unittest.main()
