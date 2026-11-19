# RTOS Observatory · 0.2.0 foundation

**Independent C++20 RTOS diagnostics engine, C11 target ring, and Dear ImGui native + browser-based offline visualisation.**

> Status: executable **offline MVP**, not a production debugger. The supplied capture is **synthetic**. This repository does **not** implement live GDB transport, a real FreeRTOS/TI-RTOS/Zephyr parser, verified trace firmware, or an installed Eclipse extension. The experimental Eclipse handoff source is in `bridge/eclipse`.

## W05 — Standalone native desktop (merged PR #12)

The Dear ImGui renderer was merged in [PR #14](https://github.com/niwantha33/RTOS-Observatory/pull/14), replacing the historical Qt desktop UI.

The optional **Dear ImGui + GLFW + OpenGL3** application provides a real Windows/Linux desktop session browser that calls the existing **C++20 core in-process**. It opens normalized trace files and indexed offline sessions, displays tasks/events/findings and evidence quality, filters event rows, and exports reports. It does **not** connect to GDB, claim live tracing, or implement W07 causal graphs.

``sh
git submodule update --init --recursive
cmake -S . -B build-desktop -DOBSERVATORY_BUILD_DESKTOP=ON -DOBSERVATORY_BUILD_LIVE=OFF
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
# Launch rtos-observatory-desktop (build-desktop/Release/ on Visual Studio generators)
```

No Qt installation is required. Dear ImGui and GLFW are exact-commit Git submodules under `third_party/` (along with optional standalone Asio). Run `git submodule update --init --recursive` after cloning or changing branches. CMake never downloads these libraries during configuration. **Windows needs MSVC-compatible SDK tools (including mt.exe and rc.exe), CMake, Ninja, and a C++20 compiler.** For clang-cl, use the checked-in `tools/build-windows-clang.ps1` from ordinary PowerShell after submodule checkout. It configures the complete MSVC/SDK environment and runs the desktop build and CTest; see [Windows details](docs/DESKTOP_IMGUI.md). [ImGui build steps and licence notes](docs/DESKTOP_IMGUI.md). W05 was originally merged with Qt in [PR #12](https://github.com/niwantha33/RTOS-Observatory/pull/12); this migration replaces that UI.

## W06 — Execution timeline and CPU analysis (merged PR #13; renderer migration in progress)

W06 adds **native Dear ImGui task and per-core execution timelines**, explicit ISR/idle/unknown spans, completed wait lanes, microsecond window selection, Fit/zoom, hover details, per-core CPU busy/idle/ISR/unknown durations and conditional utilisation percentages. Uncovered time is **Unknown**, not idle; percentages are suppressed for incomplete or loss-affected windows. Thread lanes show observed execution only, never inferred ready/sleeping states. All results remain offline evidence from normalized input.

Run the desktop with `--trace examples/timeline.trace` after building; this sample is **synthetic**. [W06 technical specification and testing](docs/W06_TIMELINE.md) · [W06 merged PR #13](https://github.com/niwantha33/RTOS-Observatory/pull/13). The core W06 model and Dear ImGui renderer are merged; this remains an offline evidence viewer.

## Optional Asio TCP capture

The host can receive a bounded finite normalized trace using standalone Asio and one strand per session, validate rows and report progress during reception, then save it for offline replay. See [build and demo instructions](docs/ASIO_CAPTURE.md). This is an early W17/W18 transport foundation; device adapters and continuously updating diagnostic analysis remain planned.

## W19 — physical target qualification: deferred, evidence tooling available

The first hardware target is MSPM0G3507/XDS110. A read-only offline
[W19 evidence validator](docs/W19_HARDWARE_QUALIFICATION.md) checks SHA256
source artifacts, exact board/probe/firmware identity, and independently
reviewable overhead/throughput claims. Synthetic fixtures **never pass the
hardware gate**; a physically labelled manifest is **still only pending
independent review**. No board, RTT, probe or running-target memory is
accessed by the validator.

```sh
python tools/w19_qualification.py --manifest examples/w19-synthetic.json
```

## W18 — opt-in live trace epochs (unverified software stage)

The desktop now has a **Live trace epochs** panel, backed by a bounded
incremental W17 decoder. The optional owned TCP receiver can create a new
binary spool with `--spool capture.rtob`; open the same file with
`rtos-observatory-desktop --live capture.rtob`. A short **observed arrival-order**
lane visualization and live event/epoch/loss table update as that file grows.

Clock calibration is explicitly user-entered per epoch. **No CPU load or
RTOS task state is inferred from unqualified numeric event IDs**. There is
no automatic GDB or RTT connection, and physical board qualification remains
deferred. [W18 limitations and run instructions](docs/W18_LIVE_EPOCHS.md).

## W17 — bounded binary trace transport (source-stage)

W17 adds an explicit little-endian, CRC32-checked binary frame for W16 records and
noncoherent per-lane loss counters. A static C11 decoder detects frame gaps,
bad CRC, duplicated sequences and epoch changes. A one-frame nonblocking sink
contract supports **platform-supplied** UART/USB CDC/owned TCP/qualified RTT
writers; **no physical driver or RTT probe access is supplied**.

With `OBSERVATORY_BUILD_LIVE=ON`, the optional
`rtos-observatory-framed-capture` connects over an already owned numeric TCP
endpoint and reports finite wire evidence and loss. This does **not** update a
real-time UI or prove hardware support. See [W17 protocol and tests](docs/W17_TRANSPORT.md).

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
| Visual timeline, findings, waits and cycles | Offline local HTML viewer plus Dear ImGui task/ISR timeline and CPU analysis (migration PR) |
| Eclipse bridge | Opt-in Java source and PDE extension snippet; **not compiled/installed/validated** |
| Saved Zephyr CTF aggregate report import | Implemented read-only adapter for existing ZEW `offline_trace_json.py` CPU and contention JSON, without raw event reconstruction |
| Live streaming and cross-RTOS capture | Not implemented; next phases |

## Source layout

- `include/observatory/engine.hpp` public engine model
- `src/engine.cpp` bounded trace parser, scheduler and causal analysis
- `src/timeline.cpp`, `include/observatory/timeline.hpp` evidence-safe per-core accounting
- `desktop/imgui/` native Dear ImGui event, timeline and CPU investigation workspace (optional)
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
