<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — bug reporting and investigation

Use **one GitHub issue per reproducible defect**. The [master tracker](https://github.com/niwantha33/RTOS-Observatory/issues/8) links six phase trackers; defects belong to the relevant phase and W-number. A separate chat can be used for each bug, but GitHub remains the durable record.

## Bug ID and workflow

- Title convention: `[P3-BUG-001][W15] Running SRAM read fails on MSPM0/XDS110` (example only).
- Reserve the next available sequential ID within the affected phase. Check existing issues to avoid duplicates.
- Lifecycle: `Open → Investigating → Fix in progress → PR ready → Testing → Closed`. Use `Blocked` if evidence/access is missing.
- Severity: **S0** safety/data corruption/security; **S1** crash, deadlock or invalid findings; **S2** broken supported flow; **S3** UI/docs/cosmetic. Severity does not establish cause.
- Reopen a closed defect if the same scenario fails in a qualified reproducible build.

## Required report fields

| Field | What to include |
| --- | --- |
| ID / affected package | Pxx-BUG-nnn, W-number and phase issue |
| Environment | OS, toolchain/IDE, RTOS/version, MCU/board, probe/server and firmware/ELF identity |
| Expected and actual | Precise observable difference, not an inferred root cause |
| Reproduction | Minimal numbered steps or saved fixture; include repeatability |
| Evidence | Logs/trace/screenshot, acquisition method, time basis, loss and completeness |
| Safety and privacy | Redact credentials, proprietary firmware, serial numbers and private RAM |
| Investigation | Confirmed facts, suspected hypotheses, rejected alternatives |
| Fix | PR link, exact commit, risk/compatibility impact, tests |
| Validation | Regression test, hardware evidence if claimed, independent checks and reviewer |

## Rules for trustworthiness

1. Never infer that a trace or memory dump is complete just because a file exists. Record missing/unknown data.
2. Debug-memory reads must be capability-gated; avoid unapproved resets, blind peripheral reads or second GDB clients.
3. Separate simulator/synthetic-fixture results from physical-target measurements.
4. Keep root-cause **hypotheses** labelled until events/ownership/clock data support a concrete conclusion.
5. Preserve original raw evidence with integrity and exact build/symbol metadata for replay.
6. Close only after the fix is merged and the reported scenario is verified, with residual limitations documented.

## Separate-conversation workflow

For a fresh implementation or bug chat, copy the issue URL, exact repo/branch, logs and desired acceptance criteria. When the work is complete, add results/PR links back to the issue and update [PROJECT_STATUS](PROJECT_STATUS.md). Messages in another chat do not automatically update GitHub.
