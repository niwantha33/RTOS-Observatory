<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — project status

**Last verified:** 2026-11-18 14:41 UTC, on `main` at commit [`67d3229c4`](https://github.com/niwantha33/RTOS-Observatory/commit/67d3229c4a4a354f95e01a7ae4a5091c6037f1da). This is a **dated snapshot**, not an automatically refreshing dashboard.

[Master issue #8](https://github.com/niwantha33/RTOS-Observatory/issues/8) · [Master plan](MASTER_PLAN.md) · [Bug guide](BUG_TRACKING.md)

## Delivery overview

| Phase | Work packages | Verified scope/state | Phase issue | Evidence |
| --- | --- | --- | --- | --- |
| **P0 Foundation and architecture** | **W01–W04** | **Done for bounded offline foundation** | [#2](https://github.com/niwantha33/RTOS-Observatory/issues/2) | [Merged PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1), [main CI passed](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) |
| P1 Modern graphical interface | W05–W08 | Next; full standalone desktop UI not yet integrated | [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3) | Offline prototype HTML in Phase 0 |
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
| W05–W08 | Not started (P1) | Interactive desktop and graph studio are future scope |
| W09–W13 | Not started (P2) | Evidence-backed RTOS diagnosis and source graph are future scope |
| W14–W19 | Not started (P3) | Debug-probe integration and live transports require physical hardware validation |
| W20–W24 | Not started (P4) | Native vendor/version-aware RTOS adapters not yet qualified |
| W25–W27 | Not started (P5) | Rich reports, optional AI/MCP and what-if simulator future work |

**Totals:** 27 packages planned · **4 done within specified offline scope** · 23 pending. **No production live trace or physical-board qualification is claimed**.

## Recent CI / PR activity

- [PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1) merged at **14:40:29 UTC, 2026-11-18**, commit [`67d3229c4`](https://github.com/niwantha33/RTOS-Observatory/commit/67d3229c4a4a354f95e01a7ae4a5091c6037f1da).
- [`main` CI run #44326850607](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) finished **success** at **14:41:57 UTC** — Ubuntu Debug, Ubuntu Release, Windows Debug and Windows Release all built and passed CTest.
- [PR #9](https://github.com/niwantha33/RTOS-Observatory/pull/9) containing this master tracking documentation and GitHub templates remains open until reviewed/merged.

## Open constraints / risks

- The C11 ring is **single producer/single consumer**; concurrent ISR/task producers must not be attached without a safe redesign and validation.
- Input identity labels and CRC32 are not independent build attestation or cryptographic proof.
- Unknown trace intervals cannot support exact CPU load, queue depth, mutex ownership or historical memory reconstruction.
- Eclipse handoff is unqualified source, not an installed production DSF-GDB bridge.
- Windows CI **is now verified** for the offline foundation, but real MCU/probe bandwidth, overhead and live streaming remain untested.

## Update rules

Statuses: `Not started`, `In progress`, `Draft PR`, `PR ready`, `Testing`, `Blocked`, `Done`. **Done** requires merged code, passing applicable checks and bounded evidence for the declared scope. Bug statuses are separately tracked as `Open`, `Investigating`, `Fix in progress`, `PR ready`, `Testing`, `Closed`, `Blocked`.

Always update phase issue, master issue and this document with exact commit, CI results and outstanding defects. Do not treat this snapshot as live.
