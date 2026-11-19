# SPDX-License-Identifier: Apache-2.0
"""Pure state-machine gates; no GH network, agent or real hardware needed."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "tools" / "autopilot_runner.py"
spec = importlib.util.spec_from_file_location("autopilot_runner", MODULE)
auto = importlib.util.module_from_spec(spec)
spec.loader.exec_module(auto)
ROOT = MODULE.parents[1]
POLICY = auto.load_config(ROOT / "autopilot/policy.json")
QUEUE = auto.load_config(ROOT / "autopilot/queue.json")


def checks(outcome="success", pending=False):
    data = []
    for prefix, count in POLICY["required_check_groups"].items():
        for index in range(count):
            data.append({"name": f"{prefix}matrix {index})", "status": "in_progress" if pending else "completed",
                         "conclusion": None if pending else outcome})
    return {"total_count": len(data), "check_runs": data}


def pr():
    return {"baseRefName": "main", "headRefName": "autopilot/w07-causal-graph",
            "headRefOid": "abc123", "isDraft": False, "mergeable": "MERGEABLE",
            "reviewDecision": "APPROVED", "number": 21, "state": "OPEN", "url": "https://github.com/example/pull/21"}


class PolicyTests(unittest.TestCase):
    def test_valid_policy(self):
        auto.validate(POLICY, QUEUE)

    def test_reject_unsafe_or_out_of_order_configuration(self):
        for key, value in (("hardware_qualification", "passed"),
                           ("max_automatic_repair_attempts", 50),
                           ("concurrent_feature_branches", 2),
                           ("merge_policy", "unguarded")):
            bad = dict(POLICY, **{key: value})
            with self.assertRaises(ValueError):
                auto.validate(bad, QUEUE)
        with self.assertRaises(ValueError):
            auto.validate(POLICY, {"tasks": list(reversed(QUEUE["tasks"]))})

    def test_all_ten_original_ci_and_two_autopilot_jobs_required(self):
        self.assertEqual(auto.assess_checks(checks(), POLICY["required_check_groups"])[0], "PASS")
        missing = checks()
        missing["check_runs"].pop()
        missing["total_count"] -= 1
        self.assertEqual(auto.assess_checks(missing, POLICY["required_check_groups"])[0], "PENDING")
        truncated = checks()
        truncated["total_count"] += 1
        self.assertEqual(auto.assess_checks(truncated, POLICY["required_check_groups"])[0], "PENDING")

    def test_failed_cancelled_skipped_pending_blocks_merge(self):
        for conclusion in ("failure", "cancelled", "skipped", "timed_out"):
            self.assertEqual(auto.assess_checks(checks(conclusion), POLICY["required_check_groups"])[0], "FAIL")
        self.assertEqual(auto.assess_checks(checks(pending=True), POLICY["required_check_groups"])[0], "PENDING")
        extra = checks()
        extra["check_runs"].append({"name": "Security audit", "status": "completed", "conclusion": "failure"})
        extra["total_count"] += 1
        self.assertEqual(auto.assess_checks(extra, POLICY["required_check_groups"])[0], "FAIL")

    def test_merge_is_fail_closed(self):
        self.assertEqual(auto.assess_pr(pr(), ("PASS", "ok"))[0], "PASS")
        for key, value, expected in (("baseRefName", "dev", "BLOCKED"),
                                     ("headRefName", "feature/other", "BLOCKED"),
                                     ("isDraft", True, "PENDING"),
                                     ("mergeable", "CONFLICTING", "FAIL"),
                                     ("reviewDecision", "REVIEW_REQUIRED", "PENDING"),
                                     ("reviewDecision", "CHANGES_REQUESTED", "PENDING")):
            changed = dict(pr(), **{key: value})
            self.assertEqual(auto.assess_pr(changed, ("PASS", "ok"))[0], expected)
        self.assertEqual(auto.assess_pr(pr(), ("FAIL", "CI failed"))[0], "FAIL")

    def test_durable_restart_and_repair_cap_and_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            journal = auto.Journal(directory)
            controller = auto.Controller(POLICY, QUEUE, journal, directory, "")
            self.assertEqual(controller.journal.task("AUT-01")["status"], "queued")
            self.assertEqual(controller.cycle.__name__, "cycle")
            state = journal.task("W07")
            for i in range(3):
                self.assertEqual(controller.repair(QUEUE["tasks"][1], state, "CI failed"), "retry_next_cycle")
                self.assertEqual(state["repair_attempts"], i + 1)
            self.assertEqual(controller.repair(QUEUE["tasks"][1], state, "CI failed"), "blocked")
            loaded = auto.Journal(directory)
            self.assertEqual(loaded.task("W07")["status"], "blocked")
            self.assertEqual(loaded.task("W07")["repair_attempts"], 3)
            self.assertEqual(loaded.task("W07")["blocker"], "Repair budget exhausted: CI failed")

    def test_bootstrap_blocks_w07_until_merge_and_issue_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            j = auto.Journal(directory)
            c = auto.Controller(POLICY, QUEUE, j, directory, "")
            def fake_gh(*args):
                if args[0] == "issue":
                    return {"state": "OPEN"}
                return [{"title": "[AUT-01] merged", "baseRefName": "main"}]
            with patch.object(c, "gh_json", side_effect=fake_gh):
                self.assertEqual(c.cycle(), "pending")
                self.assertEqual(j.task("W07")["status"], "queued")
            with patch.object(c, "gh_json", side_effect=lambda *args: {"state": "CLOSED"} if args[0] == "issue" else []):
                self.assertEqual(c.cycle(), "pending")
                self.assertEqual(j.task("W07")["status"], "queued")

    def test_dry_run_is_non_mutating(self):
        self.assertEqual(auto.main(["--policy", str(ROOT / "autopilot/policy.json"),
                                    "--queue", str(ROOT / "autopilot/queue.json")]), 0)


if __name__ == "__main__":
    unittest.main()