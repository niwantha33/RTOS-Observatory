<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTOS Observatory — master development plan

**Planning baseline:** 2026-11-18 · **Scope:** 27 work packages across six phases (P0–P5) · **Status source:** [PROJECT_STATUS](PROJECT_STATUS.md) and [GitHub master tracker](https://github.com/niwantha33/RTOS-Observatory/issues/8).

This is a **plan**, not an implementation or hardware-qualification claim. Check linked issues, PRs, tests and exact branch/commit before marking a feature completed. In particular, [draft PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1) proposes a Phase 0 foundation; it is **not yet merged**.

## Mission and product boundaries

Build a standalone, open-source, commercially usable embedded RTOS investigation platform. Its differentiator is **evidence-backed causal explanation**: visualise what happened, show why a task was blocked or delayed, jump to supporting events/source, identify uncertainty, and compare a proposed fix against a new capture.

- **C++20**: host event ingestion, bounded decoders, session indexing, deterministic causal analytics and exports.
- **C11**: optional tiny firmware tracing hooks / bounded target buffers; keep ISR producers nonblocking.
- **Java**: small Eclipse CDT/DSF-GDB integration that **reuses** the existing debug session; never open a competing GDB/probe session.
- **Frontend**: independent, accessible desktop/graph UI; final shell and rendering library decisions belong to W05/W07, not prerequisites for the core.
- **Adapters**: Zephyr, FreeRTOS, TI-RTOS/SYS/BIOS, RT-Thread, Eclipse ThreadX, and Rust firmware/Embassy. Rust is a language, **not an RTOS**.
- **Operating modes**: offline investigation first; controlled debug-memory snapshots and independently qualified live streams later.
- **Security**: local/offline by default; consent before network or AI transfer; redact sensitive source, RAM and logs.

Every analysis must retain capture/firmware identity, event provenance, timestamps/clock domain, loss/ordering/unknown flags, method version and evidence IDs. **Observations**, **derived measurements**, **hypotheses**, and **simulation** must be distinguishable. A historical state cannot be reconstructed from a single live memory snapshot.

## Architecture

```text
Zephyr / FreeRTOS / SYS-BIOS / RT-Thread / ThreadX / Rust firmware
      |                       |
 versioned native adapters   probe/UART/USB/RTT/network/saved file
      +-----------+-----------+
                  v
      Bounded ingestion / evidence and time normalisation
                  |
         Indexed offline/live sessions
                  |
         C++20 deterministic causal engine
                  |
        +---------+------------+
        |                      |
   Interactive graph      Reports / local AI queries
   timeline + memory      JSON/HTML/MD/SARIF
        ^
        |
  Eclipse DSF-GDB bridge (read-only, debugger owner)
```

**Safety contract:** no unapproved target halt/resume/reset, no second competing debugger connection, no blind register scans, no unbounded parsers, no invented scheduler intervals, no assumed queue depth or mutex ownership. Polling memory while running may be unavailable or inconsistent; such reads must be capability-gated and labelled.

## Work breakdown structure

| Phase | ID | Work package and output | Priority | Acceptance evidence |
| --- | --- | --- | --- | --- |
| P0 | **W01** | Repository bootstrap; import previous C++20 offline prototype; CMake/CTest, CI, docs | P0 | Clean reproducible build on supported hosts, tests and reviewable PR |
| P0 | **W02** | Universal RTOS events: per-core scheduler, ISR, resource, cycle and evidence schema | P0 | Versioned contract, unit tests and explicit unknown/loss states |
| P0 | **W03** | Pluggable decoders, bounded ingestion, ordering and loss handling | P0 | Malformed/reordered/truncated input tests; no fabricated values |
| P0 | **W04** | Indexed sessions, provenance and deterministic offline replay | P0 | Identical replay results, integrity failure tests, recorded build/clock labels |
| P1 | **W05** | Standalone desktop application and session UX | P0 | Launch independently of Eclipse; supported OS and accessibility smoke |
| P1 | **W06** | CPU/thread/ISR timeline and event inspection | P0 | Source-linked, time-windowed visualisations with unknown intervals |
| P1 | **W07** | Causal Graph Studio: task, resource and evidence nodes/edges | P0 | Filter, zoom, select, evidence link and bounded graph performance |
| P1 | **W08** | Time-travel graph replay | P1 | Graph changes only on observed transitions; resets/gaps visible |
| P2 | **W09** | Drift, jitter, dispatch delay, preemption, response and deadline analysis | P0 | Independent known-answer tests; observed max is not theoretical WCET |
| P2 | **W10** | Mutex, semaphore, gate, queue, mailbox, events, notifications | P0 | RTOS/version-aware object semantics; no invented ownership/depth |
| P2 | **W11** | Priority inversion, starvation, lock-order/deadlock-candidate causal analysis | P0 | Every finding cites minimal evidence and discloses uncertainty |
| P2 | **W12** | TCB/stack/heap, architecture-specific fault and crash evidence | P1 | Exact memory/register provenance and safe reads; unwinding quality |
| P2 | **W13** | C/C++/Rust source function/call/lock graph | P1 | ELF/source identity verification and jump-to-source capability |
| P3 | **W14** | Eclipse CDT/DSF-GDB bridge and session lifecycle | P0 | No competing probe ownership or unapproved debug state changes |
| P3 | **W15** | Safe debugger memory/register/TCB acquisition | P0 | Target capability/permission, read side-effects, snapshot coherence |
| P3 | **W16** | Bounded C11 target recorder, multi-context safety and loss telemetry | P0 | Tested concurrency, overflow, target overhead and RTOS integration |
| P3 | **W17** | UART, USB, qualified RTT and network transport plugins | P0 | Framing/CRC, sequence and loss metrics, controlled ownership |
| P3 | **W18** | Live session/epoch, clock mapping, incremental decoding and redraw | P0 | Reconnect/reset/loss tests and replay-equivalence on sealed capture |
| P3 | **W19** | Real-board qualification, beginning MSPM0G3507/XDS110 | P0 | Probe + firmware + measured rate/CPU/RAM + independent timing evidence |
| P4 | **W20** | Zephyr saved-CTF / live adapter | P0 | Version-pinned CTF comparison and physical target validation |
| P4 | **W21** | FreeRTOS scheduler and object adapter | P0 | Test application + semantics/priority calibration + capture |
| P4 | **W22** | TI-RTOS/SYS/BIOS Task/Swi/Hwi/UIA adapter | P1 | Explicit format/version/licensing and verified capture |
| P4 | **W23** | RT-Thread and Eclipse ThreadX adapters | P1 | Separate versioned tests and representative capture for each |
| P4 | **W24** | Rust/Embassy/defmt workflows | P1 | Verified executor events + symbolisation, not a generic 'Rust RTOS' |
| P5 | **W25** | Reproducible evidence-linked HTML, JSON, Markdown, graph and SARIF exports | P0 | Reports reopen offline and every finding links to its evidence |
| P5 | **W26** | Optional local MCP / AI evidence investigation | P1 | Read-only bounded queries, privacy opt-in, grounded references |
| P5 | **W27** | What-if scheduling models and before/after CI regression checks | P2 | Simulations identified as such; repeatable baseline comparisons |

## Phase gates and linked trackers

| Phase | Gate to move forward | Tracker |
| --- | --- | --- |
| **P0 — Foundation and architecture** | Build, run, replay and qualify offline data without Eclipse | [#2](https://github.com/niwantha33/RTOS-Observatory/issues/2) |
| **P1 — Modern graphical interface** | Browse a verified capture with connected task/resource/time/evidence views | [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3) |
| **P2 — Intelligent diagnostics** | Prove a known-answer mutex-induced drift chain, distinguish alternatives | [#4](https://github.com/niwantha33/RTOS-Observatory/issues/4) |
| **P3 — Debugger and live hardware** | Qualified live target/probe session, measured loss/overhead, safe debug bridge | [#5](https://github.com/niwantha33/RTOS-Observatory/issues/5) |
| **P4 — Multi-RTOS ecosystem** | No RTOS marked supported without versioned semantics and target evidence | [#6](https://github.com/niwantha33/RTOS-Observatory/issues/6) |
| **P5 — AI reports and innovation** | Reports evidence-backed, AI optional/local, simulated claims separated | [#7](https://github.com/niwantha33/RTOS-Observatory/issues/7) |

Sequencing is **one gated package at a time** by default. Shared interfaces may be implemented earlier if needed, but a phase cannot be declared complete until all its acceptance criteria are satisfied. Planned sequencing is not a promise of hardware support. 

## Work-package definition of done

1. Document scope, dependencies, supported RTOS/target/IDE and known limitations in the phase issue.
2. Implement on a named branch, with tests for success, malformed input, incomplete/lost events and safe failure.
3. Run applicable CMake/CTest and static/sanitizer/CI checks; log OS/compiler and exact results.
4. Use an independent measurement when claiming timestamps, probe bandwidth, overhead, live capture or hardware behaviour. Synthetic fixtures alone do not qualify a board.
5. Open a reviewable PR linked to the package and phase issue; **do not check the phase issue box until merged and accepted**.
6. Update [PROJECT_STATUS](PROJECT_STATUS.md), including PR/commit, outstanding bugs and follow-up work.

## Known starting evidence and exclusions

- The standalone repository initially contained `README.md` on `main`. **Draft [PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1)** proposes W01–W04 and local Linux offline tests; CI/hardware qualification and merge are pending.
- The earlier exported v0.1 prototype and Zephyr Eclipse Workbench tracing tools are **inputs to review/reuse**, not automatically integrated or physically validated.
- The legacy Workbench describes saved CTF/offline analysis as partial and live production CTF streaming as not implemented. Do not silently relabel these as qualified Observatory features.
- Market leadership and innovation are product objectives, not established benchmarks or universal superiority claims.

## How to work in separate chats and fix bugs

Keep this plan and [master tracker #8](https://github.com/niwantha33/RTOS-Observatory/issues/8) as the durable record. Open a separate implementation chat for each W-number and a separate GitHub bug issue for each defect; link the phase issue, PR, tests, and exact environment. See [BUG_TRACKING](BUG_TRACKING.md). A chat's completion does **not** automatically update GitHub; record verified progress explicitly.

**Next action:** Review/qualify [Phase 0 draft PR #1](https://github.com/niwantha33/RTOS-Observatory/pull/1), then accept or fix W01–W04 individually before advancing to P1.
