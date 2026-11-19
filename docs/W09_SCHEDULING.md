<!-- SPDX-License-Identifier: Apache-2.0 -->
# W09 — Offline evidence-backed timing diagnostics

**Status:** Proposed for CI/review, stacked on W08 until that prerequisite merges. **Scope:** the normalized, sealed, offline event stream; not live board RTOS scheduling qualification.

## What is actually measured

- **Release jitter:** signed difference between the *observed* interval between two consecutive `TASK_RELEASE` events and the task's declared `PERIOD`. Without both events and an explicit period, unknown.
- **Release phase drift:** the analyzer's existing `phase_error_us`, relative to the first observed release and its declared period. Not an oscillator-accuracy measurement.
- **Application begin delay:** explicit `TASK_RELEASE` to `TASK_BEGIN` marker. This is not necessarily time to kernel dispatch or switch-in.
- **Observed response:** explicit `TASK_RELEASE` to `TASK_FINISH` (wall time, includes any waits). The maximum sample is not a theoretical WCET.
- **Explicit deadline exceeded:** observed response greater than an *operator-entered relative deadline* for that task. A configured period is **not** silently treated as a deadline; without an explicit deadline, the status is unknown.
- **Execution interruption pattern:** two recorded task running intervals on the same core separated by observed execution of another task, with supporting `SWITCH_OUT`/`SWITCH_IN` events. This suggests an interval interruption, but does **not** prove task readiness, priority preemption, a scheduling bug, or its root cause.

Measurements refer to decoded source-event indices with timestamp and original line available in the existing Events panel. Records with missing source proof, declared trace losses, incomplete report semantics or sequence uncertainty are flagged as unverified. Known measurements are retained, but missing intervals and events are **never** synthesized.

## Desktop use

Open a saved session or normalized trace and choose **Scheduling diagnostics** in the sidebar or **Tools** menu. The summary table shows per-task observed maxima (microseconds), releases and interruption candidates. The sample table links to the exact recorded event.

To check an actual deadline, enter the observed task ID and its positive relative deadline in microseconds and click **Apply deadline**. The deadline is kept in process memory for the loaded investigation only and cleared on the next import. **Clear deadline** removes it. No deadline is inferred when that field is absent.

## Automated acceptance gates

- `observatory_scheduling_diagnostics` CTest: known-answer release jitter/phase, application begin delay, completed vs incomplete response, explicit deadline exceedance, missing/lossy evidence, event links, sample cap, malformed inputs, and a same-core interruption candidate.
- `observatory_desktop_diagnostics_smoke`: headless session/controller/diagnostics integration on synthetic fixture.
- `observatory_imgui_diagnostics_ui_smoke`: Xvfb hidden GLFW/OpenGL ImGui panel render on Linux.
- Existing native Windows/Linux Debug+Release and desktop Windows/Linux Debug+Release, plus Windows clang-cl, must pass on the latest PR head; independent review and merge required.

**Limitations:** synthetic fixture tests are not real target timing measurements. Clock provenance/firmware identity are exactly those supplied by the decoder, not attested. This version does not compute true kernel dispatch latency, formal WCET, guaranteed deadlines, causal ownership chains, or physical CPU overhead. W10/W11 will provide RTOS-specific semantics and causal proof boundaries.
