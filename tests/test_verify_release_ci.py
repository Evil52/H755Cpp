import importlib.util
import json
import unittest
from pathlib import Path
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools/verify_release_ci.py"
spec = importlib.util.spec_from_file_location("verify_release_ci", MODULE_PATH)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class ReleaseCiGateTests(unittest.TestCase):
    def setUp(self):
        self.merge = "a" * 40
        self.pr_head = "b" * 40
        self.repository = "owner/repo"
        self.main_run = {"event": "push", "headSha": self.merge, "conclusion": "success"}
        self.prs = [{
            "merge_commit_sha": self.merge,
            "head": {"sha": self.pr_head},
            "base": {"ref": "main"},
            "merged_at": "2026-11-15T00:00:00Z",
            "number": 10,
        }]
        self.runs = [{
            "workflowName": "CI", "conclusion": "success",
            "headSha": self.pr_head, "databaseId": 41,
        }]
        self.jobs = {"jobs": [
            {"name": name, "conclusion": "success"} for name in gate.REQUIRED
        ]}

    def command(self, *args):
        if args[:3] == ("gh", "run", "view") and args[3] == "42":
            return json.dumps(self.main_run)
        if args[:2] == ("git", "rev-list"):
            return f"{self.merge} {'c' * 40} {self.pr_head}"
        if args[:2] == ("gh", "api"):
            return json.dumps(self.prs)
        if args[:3] == ("gh", "run", "list"):
            return json.dumps(self.runs)
        if args[:3] == ("gh", "run", "view") and args[3] == "41":
            return json.dumps(self.jobs)
        self.fail(f"unexpected command: {args}")

    def verify(self):
        with mock.patch.object(gate, "command", side_effect=self.command):
            return gate.verify(self.repository, self.merge, 42)

    def test_accepts_exact_merged_pr_with_all_required_jobs(self):
        self.assertEqual(self.verify(), (10, 41))

    def test_rejects_wrong_main_ci_and_wrong_pr(self):
        self.main_run["headSha"] = "d" * 40
        with self.assertRaisesRegex(ValueError, "main CI"):
            self.verify()
        self.main_run["headSha"] = self.merge
        self.prs[0]["head"]["sha"] = "d" * 40
        with self.assertRaisesRegex(ValueError, "unique merged main PR"):
            self.verify()

    def test_rejects_missing_or_failed_pr_job(self):
        self.jobs["jobs"].pop()
        with self.assertRaisesRegex(ValueError, "no successful full PR CI"):
            self.verify()
        self.jobs["jobs"] = [
            {"name": name, "conclusion": "success"} for name in gate.REQUIRED
        ]
        self.jobs["jobs"][0]["conclusion"] = "failure"
        with self.assertRaisesRegex(ValueError, "no successful full PR CI"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
