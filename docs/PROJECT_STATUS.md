<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — project status

**Last verified:** 2026-11-18 against GitHub `main` and open PRs. This is a **dated snapshot**, not an automatically updating dashboard. Before marking a checkbox complete, inspect the current branch, CI, PR, merge status and evidence.

[Master issue #8](https://github.com/niwantha33/RTOS-Observatory/issues/8) · [Master plan](MASTER_PLAN.md) · [Bug guide](BUG_TRACKING.md)

## Delivery overview

| Phase | Work packages | Status at baseline | Phase issue | Known PR |
| --- | --- | --- | --- | --- |
| P0 Foundation and architecture | W01–W04 | **Draft PR / under review**, no package accepted in `main` | [#2](https://github.com/niwantha33/RTOS-Observatory/issues/2) | [#1](https://github.com/niwantha33/RTOS-Observatory/pull/1) |
| P1 Modern graphical interface | W05–W08 | Planned | [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3) | — |
| P2 Intelligent RTOS diagnostics | W09–W13 | Planned | [#4](https://github.com/niwantha33/RTOS-Observatory/issues/4) | — |
| P3 Debugger and live hardware | W14–W19 | Planned | [#5](https://github.com/niwantha33/RTOS-Observatory/issues/5) | — |
| P4 Multi-RTOS ecosystem | W20–W24 | Planned | [#6](https://github.com/niwantha33/RTOS-Observatory/issues/6) | — |
| P5 AI reports and innovation | W25–W27 | Planned | [#7](https://github.com/niwantha33/RTOS-Observatory/issues/7) | — |

## Work-package state ledger

| IDs | State | Evidence / action |
| --- | --- | --- |
| W01–W04 | **Proposed in draft PR #1** (not merged) | Review tests/CI, reconcile claims against code, merge after acceptance |
| W05–W08 | Not started | Phase #3 |
| W09–W13 | Not started | Phase #4 |
| W14–W19 | Not started | Phase #5 |
| W20–W24 | Not started | Phase #6 |
| W25–W27 | Not started | Phase #7 |

**Baseline totals:** 27 planned packages, 0 accepted/merged, 4 proposed in one draft PR, 23 not yet proposed. No verified live capture or cross-RTOS physical qualification is claimed.

## Open items and risks

- PR #1 is **draft**, and its reported Linux test passes do not establish Windows CI or real-device qualification.
- Avoid a separate competing OpenOCD/GDB owner; Eclipse bridge must follow active CDT session permissions.
- C11 ring currently proposed for single-producer/single-consumer use; ISR/task concurrent producers need design and validation before use.
- A trace with loss/unknown boundaries cannot support fabricated CPU, queue depth, lock ownership or historical memory reconstruction.
- A desktop viewer, AI queries and real-time RTOS graph are planned; the existence of an offline HTML demo is not implementation evidence for them.

## Rules for updates

Statuses: `Not started`, `In progress`, `Draft PR`, `PR ready`, `Testing`, `Blocked`, `Done`. **Done** requires merged code plus package acceptance evidence. Keep bug states separately: `Open`, `Investigating`, `Fix in progress`, `PR ready`, `Testing`, `Closed`, `Blocked`.

When a package changes state, update the phase issue checkbox, this ledger, last verified date, link the PR/commit and attach test/hardware logs. Never treat this snapshot as auto-updating.
