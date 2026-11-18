<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — project status

**Verified baseline:** 2026-11-18 — W05 merged on `main` at [`595bd157e`](https://github.com/niwantha33/RTOS-Observatory/commit/595bd157e5a3b9b86a2e11d1c6b6edd148713d67), with [desktop Windows/Linux CI passing](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44337073814). **W06 is in draft PR #13**, validation pending; this is a dated development snapshot, not a live CI dashboard.

[Master issue #8](https://github.com/niwantha33/RTOS-Observatory/issues/8) · [Master plan](MASTER_PLAN.md) · [Bug guide](BUG_TRACKING.md)

## Delivery overview

| Phase | Work packages | Verified scope/state | Phase issue | Evidence |
| --- | --- | --- | --- | --- |
| **P0 Foundation and architecture** | **W01–W04** | **Done for bounded offline foundation** | [#2](https://github.com/niwantha33/RTOS-Observatory/issues/2) | [Merged PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1), [main CI passed](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) |
| P1 Modern graphical interface | W05–W08 | **W05 done** (merged PR #12 / CI passed). **W06 draft PR #13**, awaiting CI and review. W07–W08 planned | [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3) | [W05 PR #12](https://github.com/niwantha33/RTOS-Observatory/pull/12) · [W06 PR #13](https://github.com/niwantha33/RTOS-Observatory/pull/13) |
| P2 Intelligent RTOS diagnostics | W09–W13 | Planned | [#4](https://github.com/niwantha33/RTOS-Observatory/issues/4) | — |
| P3 Debugger and live hardware | W14–W19 | Planned | [#5](https://github.com/niwantha33/RTOS-Observatory/issues/5) | — |
| P4 Multi-RTOS ecosystem | W20–W24 | Planned | [#6](https://github.com/niwantha33/RTOS-Observatory/issues/6) | — |
| P5 AI reports and innovation | W25–W27 | Planned | [#7](https://github.com/niwantha33/RTOS-Observatory/issues/7) | — |

## Package ledger

| IDs | Acceptance status | Limitations / next action |
| --- | --- | --- |
| **W01** | **Done** | C++20/C11 offline source, CMake/CTest, Windows+Ubuntu Debug/Release CI passed |
| **W02** | **Done — normalized v1 only** | Task/ISR/mutex events and generic semaphore/queue waits; not native CTF/UIA |
| **W03** | **Done — bounded offline only** | Explicit decoder registry, limits and gap/loss reporting; no running-target decoder |
| **W04** | **Done — local offline v1 only** | Indexed immutable sessions, raw evidence, CRC32 integrity and deterministic replay; no secure attestation or range API |
| **W05** | **Done (offline UI)** | Merged PR #12; Windows and Ubuntu Debug/Release CI passed; no hardware or installer qualification |
| **W06** | **Draft PR #13 / testing queued** | Offline task/ISR/idle/unknown timeline, per-core accounting, completed waits; [spec](W06_TIMELINE.md); CI, review, and merge required |
| W07–W08 | Not started (P1) | Causal graph and time-travel replay remain future scope |
| W09–W13 | Not started (P2) | Evidence-backed RTOS diagnosis and source graph are future scope |
| W14–W19 | Not started (P3) | Debug-probe integration and live transports require physical hardware validation |
| W20–W24 | Not started (P4) | Native vendor/version-aware RTOS adapters not yet qualified |
| W25–W27 | Not started (P5) | Rich reports, optional AI/MCP and what-if simulator future work |

**Totals:** 27 packages planned · **5 accepted within offline/desktop scope** · **1 W06 draft PR / unaccepted** · 21 not started. Do not count W06 done until merged and tested. **No production live trace or physical-board qualification is claimed**.

## Recent CI / PR activity

- [PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1) merged at **14:40:29 UTC, 2026-11-18**, commit [`67d3229c4`](https://github.com/niwantha33/RTOS-Observatory/commit/67d3229c4a4a354f95e01a7ae4a5091c6037f1da).
- [`main` CI run #44326850607](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) finished **success** at **14:41:57 UTC** — Ubuntu Debug, Ubuntu Release, Windows Debug and Windows Release all built and passed CTest.
- [PR #9](https://github.com/niwantha33/RTOS-Observatory/pull/9) was merged; roadmap/templates are on main.
- [W05 PR #12](https://github.com/niwantha33/RTOS-Observatory/pull/12) merged 2026-11-18; [desktop CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44337073814) passed all four Windows/Linux Debug/Release jobs.
- [W06 draft PR #13](https://github.com/niwantha33/RTOS-Observatory/pull/13) implements timeline and CPU analysis; CI and acceptance still pending.

## Open constraints / risks

- The C11 ring is **single producer/single consumer**; concurrent ISR/task producers must not be attached without a safe redesign and validation.
- Input identity labels and CRC32 are not independent build attestation or cryptographic proof.
- Unknown trace intervals cannot support exact CPU load, queue depth, mutex ownership or historical memory reconstruction.
- Eclipse handoff is unqualified source, not an installed production DSF-GDB bridge.
- Windows CI **is now verified** for the offline foundation, but real MCU/probe bandwidth, overhead and live streaming remain untested.

## Update rules

Statuses: `Not started`, `In progress`, `Draft PR`, `PR ready`, `Testing`, `Blocked`, `Done`. **Done** requires merged code, passing applicable checks and bounded evidence for the declared scope. Bug statuses are separately tracked as `Open`, `Investigating`, `Fix in progress`, `PR ready`, `Testing`, `Closed`, `Blocked`.

Always update phase issue, master issue and this document with exact commit, CI results and outstanding defects. Do not treat this snapshot as live.
