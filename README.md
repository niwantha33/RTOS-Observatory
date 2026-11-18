# RTOS Observatory · 0.2.0 foundation

**Independent C++20 RTOS diagnostics engine, C11 target ring, and Qt6 native + browser-based offline visualisation.**

> Status: executable **offline MVP**, not a production debugger. The supplied capture is **synthetic**. This repository does **not** implement live GDB transport, a real FreeRTOS/TI-RTOS/Zephyr parser, verified trace firmware, or an installed Eclipse extension. The experimental Eclipse handoff source is in `bridge/eclipse`.

## W05 — Standalone native desktop (merged PR #12)

The optional **Qt 6 Widgets** application provides a real Windows/Linux desktop session browser that calls the existing **C++20 core in-process**. It opens normalized trace files and indexed offline sessions, displays tasks/events/findings and evidence quality, filters event rows, and exports reports. It does **not** connect to GDB, claim live tracing, or implement W07 causal graphs.

```sh
cmake -S . -B build-desktop -DOBSERVATORY_BUILD_DESKTOP=ON -DOBSERVATORY_BUILD_LIVE=OFF
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
# Launch rtos-observatory-desktop (build-desktop/Release/ on Visual Studio generators)
```

Qt 6.5+ development libraries matching your compiler are required **only** for the desktop option. W05 merged in [PR #12](https://github.com/niwantha33/RTOS-Observatory/pull/12) with successful Windows/Linux desktop CI; [setup and scope](docs/DESKTOP_W05.md).

## W06 — Execution timeline and CPU analysis (draft PR #13)

W06 adds **native Qt task and per-core execution timelines**, explicit ISR/idle/unknown spans, completed wait lanes, microsecond window selection, Fit/zoom, hover details, per-core CPU busy/idle/ISR/unknown durations and conditional utilisation percentages. Uncovered time is **Unknown**, not idle; percentages are suppressed for incomplete or loss-affected windows. Thread lanes show observed execution only, never inferred ready/sleeping states. All results remain offline evidence from normalized input.

Run the desktop with `--trace examples/timeline.trace` after building; this sample is **synthetic**. [W06 technical specification and testing](docs/W06_TIMELINE.md) · [W06 draft PR #13](https://github.com/niwantha33/RTOS-Observatory/pull/13). W06 is **not accepted until CI passes and the PR merges**.

## Optional Asio TCP capture

The host can receive a bounded finite normalized trace using standalone Asio and one strand per session, validate rows and report progress during reception, then save it for offline replay. See [build and demo instructions](docs/ASIO_CAPTURE.md). This is an early W17/W18 transport foundation; device adapters and continuously updating diagnostic analysis remain planned.

## Phase 0: offline sessions

Versioned ingestion, decoder registration, bounded processing and indexed session storage are implemented. See [Foundation](docs/FOUNDATION.md) and [Universal v1](docs/UNIVERSAL_V1.md) for contracts and qualification limits.

```sh
./build/rtos-observatory --decoder universal-v1 --input examples/foundation.trace --store capture-session --output report.json
./build/rtos-observatory --replay capture-session --output replay.json
```

Build first using the platform instructions below. Reports from capture and replay are deterministic. A session preserves original raw bytes, firmware/clock labels, sequence-loss evidence, a row index and a corruption checksum. Live RTOS capture is a later phase.

## Quick start (Windows, Visual Studio 2022 or LLVM/MSVC toolchain)

In PowerShell from the extracted directory:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\rtos-observatory.exe --input .\examples\inversion.trace --output .\examples\my-report.json
```

For Ninja/single-config, the executable may be at `build/rtos-observatory.exe`. Or run `./tools/build_demo.ps1` for automatic path selection.

Open `viewer/index.html` in a browser. The built-in **synthetic demo** loads without a server or installing Node. Select **Open report JSON** to view your own generated reports. If file URLs are disabled by your organisation's browser policy, serve only the `viewer/` folder on localhost using a trusted development server; do not publish traces to the internet.

## Linux/macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/rtos-observatory --input examples/inversion.trace --output examples/my-report.json
```

## Current capabilities

| Capability | Current state |
| --- | --- |
| C++20 library + CLI | Linux GCC 14 verified; Windows/Linux Debug + Release CI configured |
| Deterministic offline trace ingestion | Implemented for **normalised event TSV** only; not raw CTF/UIA |
| Paired task scheduler intervals and nested ISR exclusion | Implemented for input with complete events |
| Mutex ownership, completed waits, priority-inversion pattern | Implemented, conservatively; no deadline/cause proof |
| Periodic release/start/finish markers and drift | Implemented for explicit application cycle markers |
| Semaphore/queue/event/notification waits | Generic paired duration only; no queue depth or ownership reconstruction |
| Trace loss, bad timestamps, missing event pairs | Reject or report incomplete; no invented reconstruction |
| Embedded C11 tracing ring | Built + tested **SPSC only**, no RTOS hooking/drain transport |
| Visual timeline, findings, waits and cycles | Offline local HTML viewer; W06 draft Qt6 scheduler/task/ISR timeline and per-core CPU analysis |
| Eclipse bridge | Opt-in Java source and PDE extension snippet; **not compiled/installed/validated** |
| Saved Zephyr CTF aggregate report import | Implemented read-only adapter for existing ZEW `offline_trace_json.py` CPU and contention JSON, without raw event reconstruction |
| Live streaming and cross-RTOS capture | Not implemented; next phases |

## Source layout

- `include/observatory/engine.hpp` public engine model
- `src/engine.cpp` bounded trace parser, scheduler and causal analysis
- `src/timeline.cpp`, `include/observatory/timeline.hpp` evidence-safe per-core accounting
- `desktop/` native Qt6 event, timeline and CPU investigation workspace (optional)
- `src/main.cpp` command-line entry point
- `firmware/obs_trace_ring.{h,c}` bounded C11 SPSC event queue
- `adapters/zew_offline_reports.py` imports existing Workbench saved-CTF **aggregate** JSON reports without inventing event boundaries
- `viewer/` independent offline graphical report viewer
- `bridge/eclipse/` sample Eclipse command handler to hand off report files
- `examples/` deterministic synthetic fixture and JSON result
- `tests/` automated engine and target ring tests
- `docs/EVENT_SCHEMA.md` normalised input event contract
- `docs/ARCHITECTURE.md` staged cross-RTOS/IDE design

## Security and correctness

* All incoming trace rows have a bounded length and record limit; unrecognised events, backward timestamps and multiple session headers are rejected.
* The priority ordering is **normalised** so larger values mean higher urgency. An actual RTOS adapter must supply a proven mapping, dynamic effective priority and task identity including reuse epochs.
* Durations and timing come from explicit paired input. We never reconstruct historical stack memory, ISR nesting beyond supplied events, queue depths, or lost intervals.
* Inversion is labelled as an **observed pattern**, not a proved deadline miss or a universally established root cause.
* A dropped-event marker marks the capture incomplete and suppresses inversion-pattern attribution.
* The offline engine and viewer open no sockets and control no target. The optional TCP capture executable connects only to an explicitly selected numeric address and sends no target commands. The Eclipse prototype does not connect to GDB.
* The demo is not a performance/overhead claim. Fuzzing, multi-core causality, transport authenticity, released binaries and physical-board qualification remain open.

## Roadmap

1. Extract the existing Zephyr saved-CTF decoder as a version-pinned adapter and compare reports with golden captures.
2. Integrate Eclipse session metadata and a read-only suspended-state DSF-GDB bridge with explicit ownership/permission controls.
3. Implement safe running-target transport with loss counters, epochs, independent firmware/build attestation and validated timing.
4. Add FreeRTOS trace-hook adapter; then SYS/BIOS UIA, RT-Thread, ThreadX and Rust/Embassy adapters separately.
5. Add correlated fault, memory, queue/notification, priority-inheritance and cross-core dependency analysis.
6. Production-release gates: fuzz, stress, multi-compiler/OS CI, latency overhead and real-hardware tests.

Apache-2.0 licensed. See `LICENSE`.

## Import existing Zephyr Workbench saved CTF analyses

Use the existing Workbench `offline_trace_json.py` (from its `tools/rtos_analysis` directory) to produce separate CPU and contention JSON files from an already saved CTF directory. Then run:

```bash
python adapters/zew_offline_reports.py --cpu cpu.json --contention contention.json --output zephyr-report.json
```

Open `zephyr-report.json` in Observatory's viewer. The importer preserves `Scheduled` as different from exclusive `Running`, carries forward the original incomplete status, and marks contention findings as aggregate summaries without fabricated event timestamps. It is **not** a CTF decoder or a qualified live link.
