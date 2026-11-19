<!-- SPDX-License-Identifier: Apache-2.0 -->
# W07 — Offline Causal Graph Studio

**Scope:** normalized offline captures loaded by the existing C++20 session controller and rendered in the Dear ImGui desktop. This feature does **not** perform live probe reads, queue reconstruction, automatic root-cause proof, or physical-target qualification.

## What the graph shows

- **Task** nodes: task IDs recorded in matching resource events.
- **Resource** nodes: mutex and generic wait resource IDs recorded in the same event.
- **Evidence** nodes: the source event's index in the decoded capture and original source line number.
- A solid task-to-resource link represents precisely one **recorded** `MUTEX_WAIT`, `MUTEX_ACQUIRE`, `MUTEX_UNLOCK`, `WAIT_BEGIN`, or `WAIT_END`. An evidence-to-entity link means the event supports that displayed observation, not that it proves a causal dependency.
- Every link carries timestamp (microseconds), source line, decoded event index and the capture clock domain; equal-time events remain in original input order.

No mutex ownership is extrapolated beyond an explicit `MUTEX_ACQUIRE` or `MUTEX_UNLOCK` event. `WAIT_END` is **not** evidence of ownership or queue depth. Missing task/resource IDs are not connected. If the decoder cannot verify sequence continuity, reports missing events, or reports drops, graph integrity is marked incomplete.

## Desktop workflow

1. Launch `rtos-observatory-desktop --trace examples/timeline.trace`, then select **Causal graph** in the navigation sidebar or **Tools > Causal graph studio**.
2. Optionally enter a task ID substring to filter event-backed relationships. The graph uses the current timeline start/end window.
3. Use the mouse wheel to zoom and middle-mouse drag to pan; **Reset graph view** restores the starting layout.
4. Select a task or resource node to list its recorded event links. Select **Inspect event** to open the exact decoded record in **Events**; evidence nodes also support direct event navigation.
5. **Show evidence** toggles event nodes without changing stored provenance.

The graph is **bounded**, not an unbounded million-node layout. Defaults: at most 360 nodes, 900 edges and 100,000 scanned events; when clipped, the UI explicitly marks omitted relationships as unknown. A UI-only cap does not alter the underlying decoded capture or existing W06 timeline accounting.

## Verification

- `observatory_graph_evidence`: deterministic model, same timestamps, missing sequence metadata, declared losses, truncation, invalid/unlinked events, empty inputs, filters, invalid time windows.
- `observatory_desktop_graph_smoke`: headless session/controller/graph integration on the synthetic normalized `examples/timeline.trace`.
- `observatory_imgui_graph_ui_smoke`: hidden GLFW/OpenGL graph render in Linux Xvfb.
- Existing Windows/Linux Debug/Release, Windows clang-cl Debug/Release, and project CTest suites must remain green before approval.

**Acceptance label:** Offline only, synthetic-fixture-tested until independent CI/review. Hardware and RTOS-vendor adapters remain unqualified. W08 time travel is separate.
