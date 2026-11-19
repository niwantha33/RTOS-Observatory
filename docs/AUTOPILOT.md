<!-- SPDX-License-Identifier: Apache-2.0 -->
# AUT-01 — External persistent Autopilot controller

**Status:** implementation candidate, **not running as a hosted service**. The controller is only continuous while an operator deploys it on an always-on Windows/Linux machine. It is not a ChatGPT background process or a GitHub scheduled workflow. No real-board or live RTOS evidence is claimed.

## Policy (19 November 2026)

Source of truth: [`autopilot/policy.json`](../autopilot/policy.json). **Continuous** operation; **external** coding-agent executable; at most **one** active `autopilot/` feature branch; **three** repairs maximum per package (in addition to the first implementation attempt); **gated** merge; **deferred hardware qualification**; block a failed task and all dependent packages. Task order is [`AUT-01`, `W07`](../autopilot/queue.json). AUT-01 is a bootstrap: its GitHub issue #18 must be closed *and* an AUT-01 PR merged to `main` before W07 may be dispatched.

### Requirements

* A dedicated, trusted host with **Python 3.10+**, **Git**, **GitHub CLI `gh`**, compiler/build tools, and a configured **external coding agent** available on the PATH. The agent command is never fetched or installed by this repo.
* `gh auth login` with least-privilege access to clone, create branches/PRs, read checks/reviews and merge. Configure the `origin` remote and a clean checkout of `main`; initialise pinned submodules using `git submodule update --init --recursive`.
* A reviewer **other than the PR author** who can approve PRs. Automatic merge is intentionally impossible without an approved review, even if tests are green. Do not bypass this by disabling the review gate. GitHub branch protection should separately enforce required status checks and disallow direct pushes to `main`.
* Enable GitHub Actions for the three workflows: native, desktop, and autopilot tests. CI availability, reviewer approval and a successful hosted run are not guaranteed by this code.

### Verify configuration without changing GitHub

```powershell
python tools/autopilot_runner.py --policy autopilot/policy.json --queue autopilot/queue.json
python -m unittest discover -s tests -p test_autopilot_runner.py -v
```

### Single-cycle host smoke (after AUT-01 is merged, reviewed and its issue accepted)

```powershell
python tools/autopilot_runner.py --policy autopilot/policy.json --queue autopilot/queue.json `
  --repo-dir . --state-dir "$HOME/.rtos-observatory-autopilot" `
  --agent-command "your-agent-executable" --execute --once
```

### Continuous host service (same command, without `--once`)

```powershell
python tools/autopilot_runner.py --policy autopilot/policy.json --queue autopilot/queue.json `
  --repo-dir . --state-dir "$HOME/.rtos-observatory-autopilot" `
  --agent-command "your-agent-executable" --execute
```

Register that command with your host's service manager (e.g. Windows Task Scheduler **At startup** with restart-on-failure, or a Linux systemd service). Protect credentials and working directory, redirect standard output/error to private logs, set the service working directory to the repo root, use a dedicated non-admin user, and keep GitHub tokens out of command-line arguments. The process checks GitHub every **900 seconds** by default. GitHub Actions CI is remote; the controller requires completed checks on the current PR SHA before merging. CI/job failures are not secretly treated as passes.

The external agent receives these environment variables: `AUTOPILOT_TASK_ID`, `AUTOPILOT_TASK_ISSUE`, `AUTOPILOT_BRANCH`, `AUTOPILOT_TASK_DESCRIPTION`, `AUTOPILOT_REPAIR_REASON`, `AUTOPILOT_ATTEMPT`, `AUTOPILOT_HARDWARE_QUALIFICATION`. The agent **must** edit and commit only on the selected branch, leave a **clean** worktree, and must not merge or push to `main`. The controller pushes the agent branch, creates a PR if missing, monitors GitHub CI and awaits independent review. The agent may read the tracked issue and [`W07_AGENT_BRIEF.md`](W07_AGENT_BRIEF.md) for scope and evidence rules.

### Safe state machine

`bootstrap pending` → `agent_running` → `awaiting_ci` → `gate_pending` → `merged` → `done` → next work package. Failed agent/CI attempts lead to `awaiting_repair` (up to 3) or `blocked`. On block, record the exact reason, leave the current work intact and stop dependent tasks. Unknown/absent checks, missing CI matrix jobs, cancelled/skipped checks, pending approvals, draft PRs and unconfirmed mergeability **never** count as a pass. PR SHA is rechecked before merging. A missing/mismatched branch or infrastructure error stops safely.

Durable state and audit logs live outside the source checkout (default: `~/.rtos-observatory-autopilot/state.json` and `events.jsonl`); no source files are modified by the runner except the agent's branch. A lock prevents two runner processes from acting concurrently. After an unclean shutdown verify there is no second runner before clearing a stale lock file. **Do not manually edit counters** to defeat the retry budget.

**No claim of full production qualification:** this controller does not prove GitHub service availability, inspect all review/supply-chain risks, create a persistent machine, guarantee Windows/Linux builds, or validate embedded hardware. Hardware gates must be added independently to the release process when physical test hardware is ready. The deferred flag is never a passed flag.