<!-- SPDX-License-Identifier: Apache-2.0 -->
# Adapter-first shared workbench — architecture and qualification

## Pipeline

~~~mermaid
flowchart TB
  UI["RTOS Observatory / Choose adapter → Browse → Apply"]
  Z["Zephyr CTF metadata + channel0_0"]
  N["Normalized trace / saved session"]
  L["W18 binary spool"]
  F["Future RTOS adapters (not implemented)"]
  UI --> Z & N & L & F
  Z --> P["Provenance-preserving ToolSummary projection"]
  N --> P
  L --> P
  F -.future.-> P
  P --> C["Evidence and capability gates"]
  C --> T["One shared graphical workbench"]
  T --> A["Dashboard / CPU where qualified"]
  T --> B["Thread / ISR / resource timelines"]
  T --> D["Mutex / semaphore / queue inspector"]
  T --> E["Observed evidence relationships"]
  T --> G["Events / findings / original evidence"]
  T --> R["Dockable / floating / font and DPI adaptive UI"]
~~~

The source adapters DO NOT create their own tool menus. They populate a bounded
read-only ToolSummary defined in include/observatory/desktop/unified_view.hpp.
The shared renderer is desktop/imgui/workspace_shared.cpp. Future decoders
extend this one projection contract, not the graphical UI.

## Evidence safety

- Normalized input preserves microsecond schema units, task intervals, completed
  waits and typed resource observations. CPU usage is only exposed when the
  qualified timeline reports sufficient complete, loss-qualified evidence.
- Zephyr CTF preserves original tick units, event indices, byte offsets and
  recorded callbacks. Resource links represent matching observed callbacks,
  not causality, ownership, calibrated latency or CPU utilization.
- Binary spool preserves raw publisher epochs and numeric event IDs. It does
  not assert task state, cross-epoch chronology or scheduler metrics.
- Incomplete source evidence must be shown as missing/unknown rather than
  inferred. Pending/unmatched waits do not become completed duration bars.
- Shared indices are built once on source refresh, not reconstructed every frame.
  UI searches and displays are bounded.

## Responsive / accessible UI

- Default 120% text, slider 100–220%, presets 100 / 120 / 150 / 180%.
- Ctrl+Plus / Ctrl+Minus keyboard zoom.
- Font-relative layout planning: normal sidebar vs compact tool picker,
  two-column resource inspector vs vertical tabs, wrapped warnings.
- Dear ImGui 1.92 per-viewport DPI font scaling and original-style rescaling
  for changing display scale; experimental mixed-DPI behavior still needs
  physical Windows multi-monitor checking.
- Qualification matrix: 1280x800, 1920x1080, 2560x1440, 4K; 125 / 150 / 200%
  OS scaling; 100 / 150 / 180% user zoom; narrow window drag; detached windows.

## Verification gates

- Shared adapter/layout CTest covers source units, safe capability flags,
  source event links, unresolved resources and font-relative breakpoints.
- Linux Xvfb UI smoke covers shared CTF dashboard, resource view, timeline,
  evidence graph and detached panels.
- Windows and Linux C++ desktop CI must pass before merge.
- Production hardware qualification remains deferred. Never report as passed.

Extension recipe: add a bounded decoder, preserve source evidence and native
clock domain, populate ToolSummary, register an import adapter, and add trace
fixtures plus UI tests. Never claim an RTOS feature based on brand alone.
