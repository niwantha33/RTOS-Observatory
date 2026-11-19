<!-- SPDX-License-Identifier: Apache-2.0 -->
# W11 — Evidence-backed causal patterns (offline)

**State:** Proposed W11, acceptance requires CTest and independent review. W08–W10 have merged to main; this package uses the existing `DecodedTrace`, `Report`, and Dear ImGui investigation workspace.

## Patterns and proof boundaries

| Pattern | Minimum recorded evidence | Meaning / exclusions |
| --- | --- | --- |
| `PRIORITY_INVERSION_EXECUTION_PATTERN` | Explicit high-urgency `MUTEX_WAIT`, pre-existing lower-urgency owner acquisition, matching owner unlock, waiter acquisition, recorded medium-urgency execution during ownership, stable normalized priorities | Observed pattern **only**; medium execution need not have caused the wait, priority inheritance/ceiling semantics are not reconstructed, and multiple cores are **not** causally qualified |
| `CIRCULAR_MUTEX_WAIT_CANDIDATE` | A closed chain of recorded task wait-on-mutex and mutex acquired-by-task edges in the scanned event snapshot | **Potential circular wait**, not a proven deadlock or guarantee that a scheduler stopped making progress |
| `MUTEX_WAIT_IN_APPLICATION_BEGIN_DELAY` | `TASK_RELEASE`, `MUTEX_WAIT`, `MUTEX_ACQUIRE`, `TASK_BEGIN` ordered as release → wait → acquire → application begin | Temporal overlap, **not** proof the mutex caused drift; `TASK_BEGIN` is an application marker, not necessarily RTOS dispatch |
| `LONG_COMPLETED_MUTEX_WAIT` | A matched explicit wait/acquire longer than the user-supplied threshold | Long observed wait, **not** proven starvation |
| `LONG_UNCLOSED_MUTEX_WAIT` | Unpaired wait observed until later capture events and a user-supplied threshold, without an event-loss/cap ambiguity | Candidate only; the task may have progressed outside the capture |
| Mutex anomalies | Repeated wait, unmatched unlock, conflicting acquisition | Instrumentation/semantic mismatch candidate; no unsupported recursive-mutex claim |

Numeric priority ordering follows the **normalized v1 model** where larger numbers indicate more urgent tasks. An adapter must verify original vendor priority direction, task identity reuse, time base, and RTOS version first. The `MUTEX_WAIT` and `MUTEX_ACQUIRE` events are required for a completed wait; no queue depth, stack value, resource count, or current kernel state is guessed.

## Trace integrity and safety

The bounded analyzer examines at most 100,000 events, 2,000 pending tasks, 20,000 candidate execution intervals and emits at most 1,000 patterns per invocation. It preserves exact decoded event indices in chronological file order, source lines/timestamps from the immutable capture, and the recorded clock-domain label. Any `DROP`, missing sequence metadata, declared loss, incomplete analyzer report or other loss invalidates complete-evidence confidence; a cap produces an explicit warning. A `DROP` clears ownership and pending waits, rather than connecting events across a gap. Contradictory ownership remains unknown.

**Do not mistake this for a real-time guarantee, external time synchronization, scheduler readiness proof, physical measurement, deadlock proof, WCET, or production-ready RTOS adapter.** Hardware qualification is deferred.

## Desktop

Open a normalized capture and choose **Causal diagnostics** under Tools or the navigation sidebar. Select a finding to inspect its explanation and supporting source-event rows. Clicking **Inspect event** opens the exact record in Events.

The default long-wait threshold is **0 (off)**. Supply an explicit threshold in microseconds to show prolonged completed/unclosed wait candidates. Reset or import a new trace to clear the threshold. The neighboring W07 **Causal graph** remains an event relationship explorer; W11 findings add bounded, explainable pattern detection, not a new live graph.

## Synthetic evidence and qualification

- `examples/causal-drift.trace`: high/medium/low mutex wait, medium CPU execution, matched owner release, application begin delay and period-relative release drift.
- `examples/causal-cycle.trace`: A owns M1, B owns M2, A waits for M2 and B waits for M1 (potential circular wait).
- `observatory_causal_patterns`: known-answer inversion, presence/absence of medium execution, deadline-independent wait threshold, cyclic mutex chain, declared loss, source-event IDs, bounded cap and invalid limits.
- `observatory_desktop_causal_smoke`: normalized decoder + desktop model on the drift fixture.
- `observatory_imgui_causal_ui_smoke`: hidden GLFW/OpenGL Dear ImGui render in Linux Xvfb.

Before merge: **Windows/Linux Debug+Release CTest**, Windows clang-cl Debug+Release, independent review of source references and interpretation. A GitHub Actions job remaining queued is not a passing test.

Future W12 extends TCB, stack and architecture-specific faults; W13 source/symbol relationships. Neither is provided here.
