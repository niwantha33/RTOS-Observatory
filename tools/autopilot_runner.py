#!/usr/bin/env python3
"""AUT-01: fail-closed external coding-agent controller; requires Python 3.10+ and gh.

This program never claims hardware qualification. It does not host a coding agent;
configure a persistent host/service and pass an agent executable explicitly.
"""
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
import os
import shlex
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def load_config(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def validate(policy, queue):
    expected = {
        "operating_mode": "continuous",
        "hardware_qualification": "deferred",
        "max_automatic_repair_attempts": 3,
        "merge_policy": "gated",
        "concurrent_feature_branches": 1,
        "coding_agent": "external_persistent_runner",
        "on_blocked_task": "record_blocker_and_stop_dependent_work",
    }
    for key, value in expected.items():
        if policy.get(key) != value:
            raise ValueError(f"Policy {key!r} must be {value!r}")
    if not isinstance(policy.get("poll_seconds"), int) or policy["poll_seconds"] < 30:
        raise ValueError("poll_seconds must be an integer >= 30")
    if not policy.get("repository") or not policy.get("required_check_groups"):
        raise ValueError("Repository and required CI groups are mandatory")
    if any(not isinstance(n, int) or n < 1 for n in policy["required_check_groups"].values()):
        raise ValueError("Each CI group must specify a positive required count")
    tasks = queue.get("tasks", [])
    if [task.get("id") for task in tasks][:2] != ["AUT-01", "W07"]:
        raise ValueError("Queue must start with AUT-01 then W07")
    ids = [task["id"] for task in tasks]
    if len(ids) != len(set(ids)):
        raise ValueError("Duplicate work package")
    for index, task in enumerate(tasks):
        if any(dep not in ids[:index] for dep in task.get("dependencies", [])):
            raise ValueError(f"Unmet/out-of-order dependency for {task['id']}")
        if task.get("kind") not in ("bootstrap", "agent") or not isinstance(task.get("issue"), int):
            raise ValueError(f"Invalid task {task['id']}")
        if task["kind"] == "agent" and not task.get("branch", "").startswith("autopilot/"):
            raise ValueError("Agent branches must use autopilot/ prefix")


def assess_checks(checks, required_groups):
    """Return PASS, PENDING, or FAIL. Unknown/missing checks fail closed."""
    if not isinstance(checks, dict) or checks.get("total_count", 0) > len(checks.get("check_runs", [])):
        return "PENDING", "Incomplete CI check-run response"
    runs = checks.get("check_runs", [])
    if not isinstance(runs, list):
        return "PENDING", "Missing CI runs"
    for prefix, minimum_count in required_groups.items():
        group = [r for r in runs if r.get("name", "").startswith(prefix)]
        if len(group) < minimum_count:
            return "PENDING", f"CI group {prefix!r} missing ({len(group)}/{minimum_count})"
        if any(r.get("status") != "completed" for r in group):
            return "PENDING", f"CI group {prefix!r} still running"
        if any(r.get("conclusion") != "success" for r in group):
            return "FAIL", f"CI group {prefix!r} has a non-success conclusion"
    # Do not disregard additional checks on the commit (for example a security gate).
    if any(r.get("status") != "completed" for r in runs):
        return "PENDING", "Other CI checks still running"
    if any(r.get("conclusion") != "success" for r in runs):
        return "FAIL", "Another CI check failed, was skipped, or was cancelled"
    return "PASS", "All required and visible CI checks succeeded"


def assess_pr(pr, checks_result):
    """Never merge on a draft, incorrect base, unknown conflict, or unapproved PR."""
    if pr.get("baseRefName") != "main" or not pr.get("headRefName", "").startswith("autopilot/"):
        return "BLOCKED", "PR branch/base violates policy"
    if pr.get("isDraft"):
        return "PENDING", "Draft PR awaiting readiness"
    if pr.get("mergeable") == "CONFLICTING":
        return "FAIL", "PR has a merge conflict"
    if pr.get("mergeable") != "MERGEABLE":
        return "PENDING", "Mergeability not confirmed"
    if checks_result[0] != "PASS":
        return checks_result
    if pr.get("reviewDecision") != "APPROVED":
        return "PENDING", "Independent approving review required"
    if not pr.get("headRefOid") or not pr.get("number"):
        return "BLOCKED", "Missing PR head or number"
    return "PASS", "Gated merge approved"


class Journal:
    def __init__(self, directory):
        self.directory = Path(directory).expanduser().resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.path = self.directory / "state.json"
        self.log = self.directory / "events.jsonl"
        self.data = load_config(self.path) if self.path.exists() else {"tasks": {}}

    def task(self, identifier):
        return self.data["tasks"].setdefault(identifier, {"status": "queued", "repair_attempts": 0})

    def event(self, identifier, action, detail):
        entry = {"time_utc": utc_now(), "task": identifier, "action": action, "detail": detail}
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry, sort_keys=True) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        print(json.dumps(entry, sort_keys=True), flush=True)

    def save(self):
        temp = self.path.with_suffix(".tmp")
        with temp.open("w", encoding="utf-8") as stream:
            json.dump(self.data, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, self.path)

    def blocked(self, identifier, reason):
        current = self.task(identifier)
        current.update(status="blocked", blocker=reason)
        self.event(identifier, "blocked", reason)
        self.save()


class Controller:
    def __init__(self, policy, queue, journal, worktree, agent_command):
        self.policy = policy
        self.queue = queue["tasks"]
        self.journal = journal
        self.worktree = Path(worktree).expanduser().resolve()
        self.agent_command = shlex.split(agent_command) if agent_command else []

    @staticmethod
    def command(argv, cwd=None, env=None):
        p = subprocess.run(argv, cwd=cwd, env=env, text=True, capture_output=True, check=False)
        if p.returncode != 0:
            raise RuntimeError(f"{' '.join(argv[:3])}: exit {p.returncode}: {p.stderr.strip()[:500]}")
        return p.stdout.strip()

    def gh_json(self, *args):
        return json.loads(self.command(["gh", *args], cwd=self.worktree))

    def check_bootstrap(self, item):
        identifier = item["id"]
        state = self.journal.task(identifier)
        if state["status"] == "done":
            return "done"
        issue = self.gh_json("issue", "view", str(item["issue"]), "--repo", self.policy["repository"], "--json", "state")
        prs = self.gh_json("pr", "list", "--repo", self.policy["repository"], "--state", "merged", "--limit", "100", "--json", "title,baseRefName")
        has_merge = any("AUT-01" in p.get("title", "") and p.get("baseRefName") == "main" for p in prs)
        if issue.get("state") == "CLOSED" and has_merge:
            state["status"] = "done"
            self.journal.event(identifier, "accepted", "Merged AUT-01 PR and closed tracking issue verified")
            self.journal.save()
            return "done"
        self.journal.event(identifier, "waiting_bootstrap", "AUT-01 must have merged PR and closed issue #18")
        return "pending"

    def ensure_exclusive(self, branch):
        prs = self.gh_json("pr", "list", "--repo", self.policy["repository"], "--state", "open", "--limit", "100", "--json", "headRefName")
        other = [p.get("headRefName") for p in prs if p.get("headRefName", "").startswith("autopilot/") and p.get("headRefName") != branch]
        if other:
            raise RuntimeError(f"Other active Autopilot feature branch: {other}")

    def get_pr(self, branch):
        # A missing PR is normal while the agent is preparing its first commit.
        p = subprocess.run(["gh", "pr", "view", branch, "--repo", self.policy["repository"],
                            "--json", "number,url,headRefName,headRefOid,baseRefName,reviewDecision,isDraft,mergeable,state,mergedAt"],
                           cwd=self.worktree, capture_output=True, text=True, check=False)
        return json.loads(p.stdout) if p.returncode == 0 else None

    def invoke_agent(self, item, state, reason):
        if not self.agent_command:
            self.journal.blocked(item["id"], "External coding agent command not configured")
            return False
        branch = item["branch"]
        env = os.environ.copy()
        env.update(AUTOPILOT_TASK_ID=item["id"], AUTOPILOT_TASK_ISSUE=str(item["issue"]),
                   AUTOPILOT_BRANCH=branch, AUTOPILOT_TASK_DESCRIPTION=item["description"],
                   AUTOPILOT_REPAIR_REASON=reason,
                   AUTOPILOT_ATTEMPT=str(state.get("agent_attempts", 0) + 1),
                   AUTOPILOT_HARDWARE_QUALIFICATION="DEFERRED")
        if state["status"] == "queued":
            if self.command(["git", "status", "--porcelain"], cwd=self.worktree):
                self.journal.blocked(item["id"], "Local git worktree has uncommitted changes")
                return False
            self.command(["git", "fetch", "origin", "main"], cwd=self.worktree)
            self.command(["git", "switch", "main"], cwd=self.worktree)
            self.command(["git", "merge", "--ff-only", "origin/main"], cwd=self.worktree)
            self.command(["git", "switch", "-c", branch], cwd=self.worktree)
        else:
            current_branch = self.command(["git", "branch", "--show-current"], cwd=self.worktree)
            if current_branch != branch:
                self.journal.blocked(item["id"], "Existing branch mismatch: manual intervention required")
                return False
        state["status"] = "agent_running"
        state["agent_attempts"] = state.get("agent_attempts", 0) + 1
        self.journal.save()
        self.journal.event(item["id"], "agent_start", f"Attempt {state['agent_attempts']}, reason={reason}")
        proc = subprocess.run(self.agent_command, cwd=self.worktree, env=env, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            self.journal.event(item["id"], "agent_failed", f"Exit {proc.returncode}: {proc.stderr[-800:]}")
            return False
        if self.command(["git", "branch", "--show-current"], cwd=self.worktree) != branch:
            self.journal.blocked(item["id"], "Agent switched branches unexpectedly")
            return False
        if self.command(["git", "status", "--porcelain"], cwd=self.worktree):
            self.journal.event(item["id"], "agent_failed", "Agent left uncommitted files; no auto-staging")
            return False
        if int(self.command(["git", "rev-list", "--count", "origin/main..HEAD"], cwd=self.worktree)) < 1:
            self.journal.event(item["id"], "agent_failed", "No new commit relative to origin/main")
            return False
        self.command(["git", "push", "-u", "origin", branch], cwd=self.worktree)
        if self.get_pr(branch) is None:
            self.command(["gh", "pr", "create", "--repo", self.policy["repository"],
                          "--base", "main", "--head", branch,
                          "--title", f"[{item['id']}] {item['description'][:70]}",
                          "--body", f"Refs #{item['issue']}\n\nExternal Autopilot agent. Hardware qualification DEFERRED. Software CI and independent review must pass."], cwd=self.worktree)
        state["status"] = "awaiting_ci"
        self.journal.save()
        self.journal.event(item["id"], "agent_submitted", "Agent changes pushed to gated PR")
        return True

    def cycle(self):
        for item in self.queue:
            identifier = item["id"]
            state = self.journal.task(identifier)
            if state["status"] == "blocked":
                return "blocked"
            if state["status"] == "done":
                continue
            if any(self.journal.task(dep)["status"] != "done" for dep in item.get("dependencies", [])):
                return "dependency_pending"
            if item["kind"] == "bootstrap":
                return self.check_bootstrap(item)
            branch = item["branch"]
            try:
                self.ensure_exclusive(branch)
                pr = self.get_pr(branch)
                if state["status"] == "awaiting_repair" or (pr is None and state["status"] in ("queued", "agent_running")):
                    reason = state.get("repair_reason", "initial implementation")
                    if not self.invoke_agent(item, state, reason):
                        return self.repair(item, state, "Coding agent failed")
                    return "submitted"
                if pr is None:
                    return self.repair(item, state, "Expected PR is missing")
                if pr.get("state") == "MERGED" or pr.get("mergedAt"):
                    state["status"] = "done"
                    self.journal.event(identifier, "merged", pr.get("url", "PR merged"))
                    self.journal.save()
                    continue
                if pr.get("headRefName") != branch:
                    self.journal.blocked(identifier, "PR head branch differs from the queued branch")
                    return "blocked"
                checks = self.gh_json("api", f"repos/{self.policy['repository']}/commits/{pr['headRefOid']}/check-runs?per_page=100")
                result = assess_pr(pr, assess_checks(checks, self.policy["required_check_groups"]))
                if result[0] == "PASS":
                    # Re-read head and all CI checks immediately before a merge.
                    fresh = self.get_pr(branch)
                    if fresh is None or fresh.get("headRefOid") != pr["headRefOid"]:
                        return "head_changed"
                    check_again = self.gh_json("api", f"repos/{self.policy['repository']}/commits/{fresh['headRefOid']}/check-runs?per_page=100")
                    if assess_pr(fresh, assess_checks(check_again, self.policy["required_check_groups"]))[0] != "PASS":
                        return "gate_changed"
                    # GitHub also enforces branch protections, if configured.
                    self.command(["gh", "pr", "merge", str(pr["number"]), "--repo", self.policy["repository"],
                                  "--squash", "--delete-branch", "--match-head-commit", pr["headRefOid"]], cwd=self.worktree)
                    state["status"] = "done"
                    self.journal.event(identifier, "merged", pr["url"])
                    self.journal.save()
                    return "merged"
                if result[0] == "FAIL":
                    return self.repair(item, state, result[1])
                if result[0] == "BLOCKED":
                    self.journal.blocked(identifier, result[1])
                    return "blocked"
                self.journal.event(identifier, "gate_pending", result[1])
                return "pending"
            except (OSError, RuntimeError, KeyError, ValueError, json.JSONDecodeError) as error:
                self.journal.blocked(identifier, f"Infrastructure or integrity error: {error}")
                return "blocked"
        return "complete"

    def repair(self, item, state, reason):
        if state["status"] == "blocked":
            return "blocked"
        if state["repair_attempts"] >= self.policy["max_automatic_repair_attempts"]:
            self.journal.blocked(item["id"], f"Repair budget exhausted: {reason}")
            return "blocked"
        state["repair_attempts"] += 1
        state["repair_reason"] = reason
        self.journal.event(item["id"], "repair_scheduled", f"{state['repair_attempts']}/3: {reason}")
        state["status"] = "awaiting_repair"
        self.journal.save()
        # Repair runs in a later cycle, never in a tight loop.
        return "retry_next_cycle"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", default="autopilot/policy.json")
    parser.add_argument("--queue", default="autopilot/queue.json")
    parser.add_argument("--state-dir", default="~/.rtos-observatory-autopilot")
    parser.add_argument("--repo-dir", default=".")
    parser.add_argument("--agent-command", default="", help="Trusted executable + args, parsed without shell")
    parser.add_argument("--execute", action="store_true", help="Allow agent, Git and GitHub changes")
    parser.add_argument("--once", action="store_true", help="Run a single poll cycle")
    args = parser.parse_args(argv)
    policy = load_config(args.policy)
    queue = load_config(args.queue)
    validate(policy, queue)
    if not args.execute:
        print(json.dumps({"mode": "DRY_RUN", "policy": policy, "queue": [x["id"] for x in queue["tasks"]]}, indent=2))
        return 0
    journal = Journal(args.state_dir)
    # Fail closed after an unclean exit: operator must verify and clear stale lock.
    lock = journal.directory / "runner.lock"
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        print(f"Another runner may be active: {lock}", file=sys.stderr)
        return 2
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(f"pid={os.getpid()} started={utc_now()}\n")
        runner = Controller(policy, queue, journal, args.repo_dir, args.agent_command)
        while True:
            outcome = runner.cycle()
            journal.save()
            if args.once or outcome in ("complete", "blocked"):
                return 0 if outcome != "blocked" else 3
            time.sleep(policy["poll_seconds"])
    except KeyboardInterrupt:
        return 130
    finally:
        lock.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())