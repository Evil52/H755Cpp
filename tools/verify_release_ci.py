#!/usr/bin/env python3
"""Require successful PR CI for the exact parent of a main merge commit."""

import argparse
import json
import subprocess
from pathlib import Path

REQUIRED = {
    "Host tests and analysis", "Semgrep", "Workflow and secrets",
    "CodeQL (c-cpp)", "CodeQL (python)", "CodeQL (actions)", "Quality gate",
}


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def verify(repository: str, merge_commit: str, main_ci_run: int) -> tuple[int, int]:
    main_run = json.loads(command("gh", "run", "view", str(main_ci_run),
                                  "--repo", repository,
                                  "--json", "event,headSha,conclusion"))
    if (main_run["event"] != "push" or main_run["headSha"] != merge_commit
            or main_run["conclusion"] != "success"):
        raise ValueError("main CI did not succeed on the requested merge commit")
    parents = command("git", "rev-list", "--parents", "-n1", merge_commit).split()
    if len(parents) != 3 or parents[0] != merge_commit:
        raise ValueError("release commit must be a two-parent merge")
    prs = json.loads(command("gh", "api", f"repos/{repository}/commits/{merge_commit}/pulls"))
    matches = [pr for pr in prs if pr["merge_commit_sha"] == merge_commit
               and pr["head"]["sha"] == parents[2] and pr["base"]["ref"] == "main"
               and pr["merged_at"]]
    if len(matches) != 1:
        raise ValueError("merge commit has no unique merged main PR")
    runs = json.loads(command(
        "gh", "run", "list", "--repo", repository, "--commit", parents[2],
        "--event", "pull_request", "--limit", "30",
        "--json", "databaseId,conclusion,headSha,workflowName"))
    for run in runs:
        if run["workflowName"] != "CI" or run["conclusion"] != "success" or run["headSha"] != parents[2]:
            continue
        detail = json.loads(command("gh", "run", "view", str(run["databaseId"]),
                                    "--repo", repository, "--json", "jobs"))
        jobs = {job["name"]: job["conclusion"] for job in detail["jobs"]}
        if all(jobs.get(name) == "success" for name in REQUIRED):
            return matches[0]["number"], run["databaseId"]
    raise ValueError("no successful full PR CI for the merged head commit")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True)
    parser.add_argument("--merge-commit", required=True)
    parser.add_argument("--main-ci-run", type=int, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    number, run_id = verify(args.repository, args.merge_commit, args.main_ci_run)
    print(f"Merged PR #{number}: all required jobs passed in CI run {run_id}")
    if args.output:
        with args.output.open("a") as output:
            output.write(f"pr_number={number}\npr_ci_run_id={run_id}\n")


if __name__ == "__main__":
    main()
