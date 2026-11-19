<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — project status

**Verified baseline:** 2026-11-19 — W01–W07 offline features merged with reported CI qualification; W08–W11 also manually merged, but their latest-head automated qualifications were queued/unverified at merger. W12 is a new PR and not merged. [PR #17](https://github.com/niwantha33/RTOS-Observatory/pull/17) improved type, contrast, spacing and language, bumped the app to v0.2.1, and merged at [commit `3f1e6c50a`](https://github.com/niwantha33/RTOS-Observatory/commit/3f1e6c50a00b8e72a97f344a98df9f71e5faba92). All **10 PR-triggered** native/desktop Windows+Linux and clang-cl CI jobs passed. This is a dated snapshot, not a live CI dashboard; post-merge UI appearance on a physical monitor is not certified.

[Master issue #8](https://github.com/niwantha33/RTOS-Observatory/issues/8) · [Master plan](MASTER_PLAN.md) · [Bug guide](BUG_TRACKING.md)

## Delivery overview

| Phase | Work packages | Verified scope/state | Phase issue | Evidence |
| --- | --- | --- | --- | --- |
| **P0 Foundation and architecture** | **W01–W04** | **Done for bounded offline foundation** | [#2](https://github.com/niwantha33/RTOS-Observatory/issues/2) | [Merged PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1), [main CI passed](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) |
| P1 Modern graphical interface | W05–W08 | W05–W08 merged (offline); W07 CI verified, W08 latest-head CI acceptance outstanding | [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3) | [W07 PR #20](https://github.com/niwantha33/RTOS-Observatory/pull/20) · [W08 PR #21](https://github.com/niwantha33/RTOS-Observatory/pull/21) |
| P2 Intelligent RTOS diagnostics | W09–W13 | W09/W10/W11 merged with outstanding qualification; W12 PR #26 pending CI/review; W13 planned | [#4](https://github.com/niwantha33/RTOS-Observatory/issues/4) | [W09 PR #22](https://github.com/niwantha33/RTOS-Observatory/pull/22) · [W10 PR #23](https://github.com/niwantha33/RTOS-Observatory/pull/23) · [W11 PR #25](https://github.com/niwantha33/RTOS-Observatory/pull/25) · [W12 PR #26](https://github.com/niwantha33/RTOS-Observatory/pull/26) |
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
| **W06** | **Merged offline** | PR #13 merged; task/ISR/unknown timeline and per-core accounting; Dear ImGui renderer PR #14 merged; shell menus/metadata PR #16 and readability update PR #17 merged ([guide](DESKTOP_IMGUI.md)) |
| **W07** | **Done (offline graph)** | PR #20 merged; 12/12 PR-head CI jobs passed. No hardware qualification |
| W08 | Merged; acceptance pending | PR #21 replay; latest-head CI/review not verified |
| W09–W10 | Merged; acceptance pending | PRs #22/#23; offline jitter/response and resource observations; post-merge CI/review not established |
| W11 | Merged; qualification pending | PR #25, merge `96fe9861a5df`; source-linked inversion/circular wait/drift patterns; latest-head CI not confirmed |
| **W12** | **PR ready — CI/review pending** | PR #26 offline versioned fault snapshots, source-linked TCB/stack/heap/register/PC/CFSR evidence; not live target acquisition |
| W13 | Not started (P2) | C/C++/Rust source relationship and symbols remain future scope |
| W14–W19 | Not started (P3) | Debug-probe integration and live transports require physical hardware validation |
| W20–W24 | Not started (P4) | Native vendor/version-aware RTOS adapters not yet qualified |
| W25–W27 | Not started (P5) | Rich reports, optional AI/MCP and what-if simulator future work |

**Totals:** 27 packages planned · **11 merged** (W01–W11), of which **7 have stated CI acceptance** (W01–W07); **W08–W11 remain software-qualification pending**, W12 PR #26 open, W13–W27 not started. **No production live trace or physical-board qualification is claimed**.

## Recent CI / PR activity

- [PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1) merged at **14:40:29 UTC, 2026-11-18**, commit [`67d3229c4`](https://github.com/niwantha33/RTOS-Observatory/commit/67d3229c4a4a354f95e01a7ae4a5091c6037f1da).
- [`main` CI run #44326850607](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44326850607) finished **success** at **14:41:57 UTC** — Ubuntu Debug, Ubuntu Release, Windows Debug and Windows Release all built and passed CTest.
- [PR #9](https://github.com/niwantha33/RTOS-Observatory/pull/9) was merged; roadmap/templates are on main.
- [W05 PR #12](https://github.com/niwantha33/RTOS-Observatory/pull/12) merged 2026-11-18; [desktop CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44337073814) passed all four Windows/Linux Debug/Release jobs.
- [Merged W06 PR #13](https://github.com/niwantha33/RTOS-Observatory/pull/13) implements offline timeline and CPU analysis.
- [Merged PR #14](https://github.com/niwantha33/RTOS-Observatory/pull/14): replaces Qt with Dear ImGui, pins ImGui/GLFW/Asio submodules.
- [Merged PR #15](https://github.com/niwantha33/RTOS-Observatory/pull/15): reproducible Windows clang-cl/MSVC SDK build and CI gates.
- [Merged PR #16](https://github.com/niwantha33/RTOS-Observatory/pull/16): File/Settings/Window/Tools/Help menus, safe dialogs, per-run settings, version/source commit/UTC configure metadata; [PR desktop CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44483212387) and [PR native CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44483212315) passed. The duplicate push runs passed too (20/20 check runs).
- [Merged PR #17](https://github.com/niwantha33/RTOS-Observatory/pull/17): v0.2.1 UI typography (system Inter where installed, OS fallbacks, readable 17 px), plain-English labels, contrast and spacing. [PR desktop CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44501894808) and [PR native CI](https://github.com/niwantha33/RTOS-Observatory/actions/runs/44501894750) all succeeded. The Linux hidden OpenGL/GLFW UI smoke passed. Physical display and multi-DPI validation remain open.

## Latest W07–W11 merge and CI audit (2026-11-19)

- **W07** [PR #20](https://github.com/niwantha33/RTOS-Observatory/pull/20) merged (commit `219cf3444572534341a0c8bc917e715aa5c33eaf`), **12/12 PR-head CI jobs passed**.
- **W08** [PR #21](https://github.com/niwantha33/RTOS-Observatory/pull/21) merged (commit `f84fc21045448f7b7207de388ce87c100ac3c9b7`); latest-head CI had remained queued, not reported as passed.
- **W09** [PR #22](https://github.com/niwantha33/RTOS-Observatory/pull/22) merged (commit `f9ed79fe9dd3992866a78693128d4831b5fc7da7`); latest-head CI status not accepted.
- **W10** [PR #23](https://github.com/niwantha33/RTOS-Observatory/pull/23) merged (commit `ab8e16801757f25dca12e817a6de7f98b2d42099`); CI not verified. W11 follow-up fixes an **incorrect CLI decoder selection** in W10's universal-v1 fixture CTest.
- **W11** [PR #25](https://github.com/niwantha33/RTOS-Observatory/pull/25) merged at `96fe9861a5dffdfd32b995aa61e9d40ef0d3add8`; latest-head CI and independent review not confirmed at time of status update.
- **W12** [PR #26](https://github.com/niwantha33/RTOS-Observatory/pull/26) open on main. Bounded offline TCB/stack/heap/memory fault evidence explorer, synthetic tests, no physical-device claims; CI and independent review pending.
- Hardware qualification **DEFERRED**, never marked passed.

## Open constraints / risks

- The C11 ring is **single producer/single consumer**; concurrent ISR/task producers must not be attached without a safe redesign and validation.
- Input identity labels and CRC32 are not independent build attestation or cryptographic proof.
- Unknown trace intervals cannot support exact CPU load, queue depth, mutex ownership or historical memory reconstruction.
- Eclipse handoff is unqualified source, not an installed production DSF-GDB bridge.
- Windows CI **is now verified** for the offline foundation, but real MCU/probe bandwidth, overhead and live streaming remain untested.

## Update rules

Statuses: `Not started`, `In progress`, `Draft PR`, `PR ready`, `Testing`, `Blocked`, `Done`. **Done** requires merged code, passing applicable checks and bounded evidence for the declared scope. Bug statuses are separately tracked as `Open`, `Investigating`, `Fix in progress`, `PR ready`, `Testing`, `Closed`, `Blocked`.

Always update phase issue, master issue and this document with exact commit, CI results and outstanding defects. Do not treat this snapshot as live.
