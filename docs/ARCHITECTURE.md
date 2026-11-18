# Architectural boundaries

## Target and source adapters

- Zephyr CTF, FreeRTOS hooks, TI-RTOS/SYS/BIOS UIA, RT-Thread, ThreadX, and Embassy are **distinct proposed adapters**. They do not share raw-event semantics or guarantee equivalent visibility.
- C11 target ring (`firmware/`) is a deliberately small **SPSC-only** mechanism, with bounded capacity and an explicit overflow counter. It is not a finished ISR-safe MPSC capture backend, packet format or transport.
- Keep source-specific IDs, timestamp domains, priority order, exact firmware/build metadata and provenance in a future versioned envelope; never silently merge boot epochs.

## Host process

1. **Capture/ingest:** source/RTOS-specific framing, metadata verification, CRC + sequence + loss + boot epoch.
2. **Normalize:** produce monotonic typed events, preserving unknown states, clock identity and source origin.
3. **Analyze:** portable C++20 engine; no assumption about the IDE, probe or vendor.
4. **Present:** browser/desktop UI consumes read-only versioned reports, separate from capture rate.
5. **Export:** sealed trace plus hashes, decoder version, evidence completeness and original source data.

Current prototype implements only a narrow offline subset of steps 2–4.

## Eclipse integration

Eclipse owns the CDT/DSF-GDB/OpenOCD lifecycle. Observatory **must never** steal it or open a second debugger connection. Phase-1 offline handler allows selecting a completed report and opening the external viewer. Future IPC will need a versioned authenticated local endpoint, process ownership, exact launch/ELF/build identity, stop/resume notifications, explicit safe read requests and access limits. A target's suspended-state snapshots cannot be presented as continuous live history.

## Cross-RTOS priorities

- Numeric priority conventions differ between RTOSes. Every adapter emits a canonical **urgency rank** where larger means higher priority; record raw priority separately in future schema.
- Mutex owner state requires explicit acquire/release evidence. A waiting task without an observed owner must never be guessed.
- Priority inversion, deadlock, priority inheritance and a missed deadline are **different** claims with different proof requirements.
- An RTOS's task control block is not a portable memory snapshot. Task switches typically save registers/context to a stack and keep a saved SP; full historical stack contents are not preserved automatically.

## Qualification gates

- End-to-end build + smoke tests on Windows x64 and Linux x64.
- Real target captures with independent timer/logic-analyser cross-check on MSPM0G3507/XDS110, then FreeRTOS target.
- Verify sequence gaps, malformed frames, reset boundaries, counter wrap and clock conversion.
- Measure on-target CPU, RAM, flash and data loss under realistic workload.
- Run protocol fuzz tests, sanitizers, concurrent-source tests, UI size limits and component licence audit.
