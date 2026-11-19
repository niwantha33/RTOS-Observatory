<!-- SPDX-License-Identifier: Apache-2.0 -->
# W08 — Offline time-travel replay

**Status:** Proposed / CI and independent review required. **Dependency:** W07 graph code, developed as a stacked branch until PR #20 is accepted.

Replay reads the same **validated normalized offline capture** used by W01–W07. It applies explicit mutex and wait transitions in recorded event order; it does not inspect a live kernel, reconstruct missing memory, infer queue depth, or assume unknown task states.

## Replay semantics

- Event cursor is an **index**, not a timestamp; two events at the same microsecond can have different snapshots.
- `MUTEX_ACQUIRE` records a last-observed owner. `MUTEX_UNLOCK` records release only when matching a recorded acquisition; otherwise it marks the owner unknown.
- `MUTEX_WAIT` / `WAIT_BEGIN` record a wait start. A matching `WAIT_END` or explicit mutex acquisition records completion. Unmatched ends remain unknown.
- `DROP` or a new `SESSION` clears the replayed relationships. Sequence gaps, reported drops, conflicting ownership or decreasing timestamps mark continuity unverified. Even a clean capture proves only the **instrumented observations**, not complete RTOS state.
- A single seek replays at most **100,000 events**; the UI caps navigation at this boundary and labels history beyond it unavailable. Immutable raw data stays unchanged.
- Selecting **Open causal graph at this event** passes **both** the timestamp and event-index boundary. This prevents graph events later in the input order but at an equal timestamp from appearing in an earlier snapshot.
- **Inspect this event** opens the exact source record in the existing Events view.

## Usage

Open a normalized trace or saved session; choose **Time-travel replay** in the left sidebar or Tools menu. Use first/previous/next/last or the slider. The resource table displays the most recent *recorded* ownership transition, and the task table displays last observed wait transitions. Use the graph/event buttons to navigate evidence.

## Qualification

The new `observatory_replay_navigation` CTest covers event order, same-time navigation, acquisition and release, wait status, gaps, contradictory ownership, budget exhaustion, empty evidence, and invalid cursor inputs. Desktop adds `observatory_desktop_replay_smoke` and Linux hidden `observatory_imgui_replay_ui_smoke`. Existing Windows/Linux Debug/Release and Windows clang-cl tests must pass for acceptance.

**Out of scope:** board/probe validation, live buffers, true historical TCB memory reconstruction, and guaranteed causal diagnosis. Hardware qualification remains **deferred**.
