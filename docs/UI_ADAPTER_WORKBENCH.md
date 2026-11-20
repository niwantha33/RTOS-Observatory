<!-- SPDX-License-Identifier: Apache-2.0 -->
# Adapter-first responsive desktop workbench

## Workflow

1. Open the **Data sources** section, choose **Zephyr CTF**, **Universal trace**, **Saved session** or **Binary event spool**.
2. The same **Connect data source** window lets users browse (Windows), edit the selected local path and press **Apply**. Selecting the adapter must not silently import a file.
3. The **Common tools** menu stays in one place. Each view uses the active evidence source and its supported semantics.
4. On narrow windows navigation collapses into the **Sources...** button and a tool selector. On wide windows it is a readable sidebar. Resizing must not produce a permanent clipped source-control toolbar.
5. Settings has a user text / UI size control; the GLFW host also scales on monitor content-scale changes. Detached windows remain available.

## Shared tool behavior

| Tool | Normalized trace | Zephyr CTF |
| --- | --- | --- |
| Dashboard | Existing normalized report | Graphical event, thread and resource counts with callback intervals |
| Timeline | Microsecond task/ISR timeline | Native CTF tick lanes for observed thread/ISR/resource intervals |
| CPU / scheduling | Qualified normalized CPU/wait report | Observed callback spans only; **never CPU utilization** |
| Mutex / semaphore / queue | Normalized resource analysis | Recorded callback intervals, linked evidence inspector |
| Events | Bounded normalized event browser | Bounded CTF browser and event-name distribution |
| Evidence | Normalized provenance | Original CTF source event, byte offsets and fields |
| Replay | Normalized time-travel | Recorded order only, no manufactured RTOS state |
| Causal diagnostics | Normalized qualified analysis | Correlation only, no ownership/deadlock inference |
| Fault / symbol / debugger tools | Independent approved input when available | Independent approved input when available |

CTF clock frequency and loss accounting are not proven by raw input; all timeline bars are labelled **raw ticks**. CTF correlation must not be shown as causality.

## Acceptance checks

- Load a local folder containing `metadata` and `channel0_0`, confirm the source selector closes only after successful Apply.
- Switch between a CTF folder and a normalized trace, ensuring the tools follow the active adapter rather than an old loaded controller.
- Try a missing/invalid folder and confirm a useful inline error without masquerading as a loaded capture.
- Exercise the graphical dashboard, timeline, resources, source-event browser and inspector, including clicking an interval to inspect source evidence.
- Resize the window wide -> narrow -> wide; verify the tool selector, sidebar and tables remain usable without forced horizontal clipping.
- Change the text scale in Settings and move the main window between monitors with different Windows scaling; verify readable controls and hit targets.
- Test CTF detached windows and the existing `--smoke-ui-ctf` / `--smoke-ui-ctf-docking` modes.
- CI Windows/Linux desktop builds and CTest must pass before merge. Hardware qualification remains deferred.
