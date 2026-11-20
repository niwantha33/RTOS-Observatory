<!-- SPDX-License-Identifier: Apache-2.0 -->
# Professional RTOS analysis UX — competitor review (20 Nov 2026)

## Evidence-backed menu/view comparison

| Product | Professional conventions worth using | Source |
| --- | --- | --- |
| Percepio Tracealyzer / View (FreeRTOS + other ports) | Separate trace acquisition and **Views** / **Layout**; linked task timelines, CPU load graphs, communications, intervals, user event plots and event log | [Percepio features](https://percepio.com/tracealyzer/features-capabilities/), [FreeRTOS integration](https://freertos.org/Documentation/03-Libraries/02-FreeRTOS-plus/05-FreeRTOS_plus_Trace/00-FreeRTOS_Plus_Trace), [Tracealyzer menu options](https://help.intervalzero.com/product_help/RTX64_4/Content/Topics/Tracealyzer/RTX64%203.6%20and%20Later/MenuOptions.html) |
| SEGGER SystemView | `File / View / Go / Target / Tool / Window / Help`; synchronized event list, timeline, CPU load and context statistics with independent windows | [SystemView 2026 manual](https://doc.segger.com/UM08027_SystemView.html), [SEGGER KB](https://kb.segger.com/SystemView) |
| Lauterbach TRACE32 OS awareness | RTOS kernel object lists, task-aware debugging, queue/semaphore views and qualified task stack usage | [OS awareness](https://www.lauterbach.com/features/os-awareness), [FreeRTOS](https://repo.lauterbach.com/rtosfreertos.html) |

**Our design deliberately resembles familiar professional workflows without copying their GUI or implying equivalent measurements.** Keep all real source-specific decoders in a separate **Adapters** tab and keep the same **Analysis** views for any source producing their evidence. Mark unimplemented native ports as *planned*, not available.

## UI hierarchy implemented

Top tabs: `Analysis | Adapters`

Native menu bar: `File | Adapters | Views | Settings | Window | Analyze | Help`

Analysis navigation:
- Overview: Dashboard
- Execution: Thread/ISR Timeline, CPU & Scheduling, Scheduling Details
- Synchronization: Kernel Objects, Communication Flow
- Investigation: Events, Findings, Evidence Inspector
- Advanced: Causal Diagnostics, Replay, Fault & Memory, Source/Symbols, Debugger, Snapshot Receipts, Event Epochs

Adapters tab: Zephyr CTF, Universal Normalized Trace, Saved Investigation Session, W18 Binary Spool. Each has **Browse and import**, then existing validation and **Apply**. Future FreeRTOS / ThreadX / TI-RTOS / RT-Thread decoders are explicitly marked future; their native binary streams cannot simply be loaded as CTF or the universal normalized schema.

## Font-size and responsive behavior

- `Settings > Text + controls`: **75–220%** with quick presets 75, 85, 100, 120, 150, 180%.
- The **Analysis** sidebar becomes a compact tool selector at narrow widths. The adapter catalogue changes between one and two columns according to font-relative width.
- Preserve OS DPI awareness and per-viewport ImGui scaling; the chosen text scale is independent of source adapter and of monitor size.
- Test with 1280×800, 1920×1080, 2560×1440 and 4K; mixed-DPI 125/150/200% Windows; 75/100/150% text.

## Why not embed HTML in Dear ImGui for the live workbench?

Dear ImGui is an immediate-mode renderer, not a web DOM. HTML needs another engine (e.g. Windows WebView2 or cross-platform Chromium/CEF). Running two input/layout/compositor stacks can increase memory, startup, packaging and focus/DPI complexity. There is **no evidence that HTML would make native trace analysis faster**. Microsoft's [WebView2 performance guide](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/performance) recommends simpler native controls for lightweight UI and batching host/JS messages to reduce bridge overhead.

**Decision:** retain one native C++20 / Dear ImGui analysis desktop. Consider [ImPlot](https://github.com/epezent/implot) (MIT, GPU accelerated and built for Dear ImGui) for future dense charts, after performance tests, licence and pinned dependency review. An optional HTML evidence **export** is a separate feature, not the source of truth for realtime views.

## Quality gates

- Native Windows/Linux build and automated regression suite must pass before merging.
- A new Xvfb headless GUI smoke renders the **Adapters** tab without requiring a private capture.
- Manually verify tab switching, native file picker, new source Apply -> Analysis, compact navigation, font presets and detached multi-monitor behavior.
- Native FreeRTOS and other new RTOS support is **not implemented** here. Never assert qualified CPU, deadlock or queue depth when absent from evidence.
