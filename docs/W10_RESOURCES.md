<!-- SPDX-License-Identifier: Apache-2.0 -->
# W10 — Resource and wait observation studio (offline)

**Implementation status:** Proposed for independent build, review and acceptance. Dependency: W09 PR #22, W08 PR #21. The graph/GUI baseline is W07, already merged. W10 is limited to **normalized offline events** and uses the existing C++20 decoder and Dear ImGui desktop.

## Seven resource categories

| Resource | How identified | What the current trace proves |
| --- | --- | --- |
| Mutex | `MUTEX_WAIT`, `MUTEX_ACQUIRE`, `MUTEX_UNLOCK` (prefixed or unprefixed IDs) | Observed acquire/unlock, matching completed waits, last recorded acquisition; no current-owner guarantee |
| Semaphore | `sem:` resource ID in `WAIT_BEGIN`/`WAIT_END` | Wait start and matching end; **not** token count, posting source, or ownership |
| Queue | `queue:` ID | Wait start/end; **not** queue depth or message content |
| Gate | `gate:` ID | Generic recorded wait start/end; **not** gate admission or nesting |
| Mailbox | `mailbox:` ID | Generic recorded wait start/end; **not** mailbox occupancy |
| Event | `event:` ID | Generic recorded wait start/end; **not** event bits, wait-all/any semantics, or triggering producer |
| Notification | `notify:` ID | Generic recorded wait start/end; **not** notification count/value or producer |

Other identifiers remain **generic/unknown**; naming is only a display classification, not proof of vendor semantics. This implementation does **not** add unqualified proprietary RTOS event types to the normalized-v1 parser. Future per-RTOS adapters must supply versioned meanings, identity lifetime, object APIs and producer/consumer evidence before adding richer semantics.

## Resource integrity rules

- Each displayed operation records the exact decoded **event index**, source line, task, resource, timestamp and trace clock domain.
- Only matched `WAIT_BEGIN` + `WAIT_END`, or `MUTEX_WAIT` + `MUTEX_ACQUIRE`, produce measured completed wait time.
- A standalone acquire never creates a fictitious wait. Unpaired ends, duplicate starts, ownership conflicts and unmatched unlocks create **anomaly candidates**, not proof of an RTOS programming fault.
- After recorded `DROP`, previous mutex observations and pending waits become **unknown**. Gaps/declared loss/analyzer incompleteness are visible; subsequent observations remain provenance-linked but don't attest uninterrupted history.
- A mutex last-acquire label is **not** a live mutex owner reading; no lock scheduling priority inheritance, recursive mutex support, event-causation proof, deadlock claim, or blocked-state reconstruction.
- Display and processing caps: first 100,000 scanned events, at most 1,000 resource IDs, 10,000 simultaneous observed waits, 20,000 operation rows and 2,000 anomalies. Truncation is signaled and does **not** silently imply completeness.
- The generic event `value` field is **not** interpreted as depth, count or kernel state.

## GUI workflow

Open a validated trace or indexed session, select **Resources and waits** from navigation or Tools, filter/select a resource, inspect its recorded wait totals/unknowns, and use **Inspect event** or **Show source** to navigate to the exact original Events row. Relevant CI smoke targets:
- `observatory_resource_semantics`: deterministic mutex, semaphore, queue, gate, mailbox, event and notification examples; confirmed waits, invalid source IDs, loss, conflicting ownership, duplicates, unknowns, limits.
- `observatory_desktop_resource_smoke`: headless controller integration with synthetic `examples/timeline.trace`.
- `observatory_imgui_resource_ui_smoke`: Linux Xvfb hidden Dear ImGui render.
- Existing Windows and Ubuntu native/desktop Debug/Release and Windows clang-cl Debug/Release must be green on the PR head.

**Explicit limitations:** Physical hardware qualification deferred. Live probe/firmware adapters, true semaphore/queue/gate internals and automatic priority-inversion root-cause proof remain future packages. W11 covers potential deadlocks and evidence-based causal investigation.
