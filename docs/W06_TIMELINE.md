<!-- SPDX-License-Identifier: Apache-2.0 -->
# W06 — RTOS execution timeline and CPU analysis (offline)

W06 extends the W05 **independent Qt6 desktop**. It consumes only reports from the existing C++20 decoder/analyzer and indexed session engine; it does not implement a second decoder, attach to target hardware, or infer unseen scheduler state.

## Build and run

    cmake -S . -B build-desktop -DOBSERVATORY_BUILD_DESKTOP=ON -DOBSERVATORY_BUILD_TESTS=ON
    cmake --build build-desktop --config Release
    ctest --test-dir build-desktop -C Release --output-on-failure
    ./build-desktop/rtos-observatory-desktop --trace examples/timeline.trace

On Windows, run the executable from the generator's output directory (for example build-desktop/Release/). The included example is synthetic **universal-v1** data and is not RTOS or hardware qualification.

Open **Execution timeline** to inspect explicitly observed CPU task, idle, and ISR spans and completed wait lanes, hover bars for source-range details, choose an exact microsecond window, Fit, or zoom. Open **CPU / waits** for independent per-core running/ISR/idle/unknown durations, conditional busy percentages, and per-task observed execution vs completed wait durations.

## Accounting rules / limits

- The visible range is a half-open interval \`[start_us, end_us)\` inside the capture. All arithmetic uses 64-bit integer microsecond timestamps; floating point is used only for rendering and display percentages.
- **Running** includes explicit task switch-in/switch-out intervals, split at ISR entry and exit by the existing analyzer. **ISR** includes explicit outermost interrupt spans, including nested ISR wall time, and is *not* charged to the task. **Idle** requires explicit idle task evidence.
- An uncovered interval or an invalid overlap is **Unknown**, not idle. Known core lanes remain visible as Unknown even in wholly uncovered windows. No inferred Ready, Sleeping, Suspended, queue depth, preemption reason or deadline status is displayed.
- Busy time = observed running + ISR wall time. A Busy % is displayed **only** when the entire selected core/window has explicit non-overlapping scheduler coverage, the analyzer reports complete, and the decoder reports no missing or dropped events. Otherwise it says Unknown. Even qualified percentages describe only the supplied trace evidence, not attested physical CPU usage.
- Per-core utilization is shown independently; there is no misleading combined multicore percentage. The universal-v1 prototype is not cross-core-causality-qualified.
- The wait table contains **completed recorded waits**, clipped to the window. They do not imply an exclusive blocked state, proven ownership, exact scheduling cause or missing waits.
- A display limit of 20,000 scheduler slices/wait bars prevents unbounded canvas work; display truncation is flagged, while accounting covers all decoded intervals. Wait visualization is limited to 32 lanes; the breakdown table remains available.
- Endpoints outside the recorded capture and empty ranges are rejected. Drops, reset epochs, unclosed scheduler spans, bad priorities, event loss, and metadata uncertainty follow the original evidence rules.

## Tests / acceptance

\`observatory_timeline_accounting\`: 64-bit window clipping, unknown gaps, ISR exclusion from task runtime, wait clipping, fully covered CPU %, lossy evidence suppression, overlapping intervals, multicore independence, invalid windows and render cap.

\`observatory_desktop_controller\`: imports and indexed sessions preserve original evidence, and the W06 controller exposes only gated percentages. \`observatory_desktop_smoke\` and \`observatory_desktop_timeline_smoke\` exercise native Qt6 offscreen creation and loading on Windows and Linux.

The acceptance target is **Windows and Ubuntu Debug and Release build + CTest green**, reviewed PR, and merge before W06 can be considered complete. Live debugger acquisition, board measurement, raw CTF, installed packaging, W07 causal graph and W08 replay remain separate work.
