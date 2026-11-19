<!-- SPDX-License-Identifier: Apache-2.0 -->
# W18 — Live epochs, raw event timeline and bounded incremental GUI

**Status: source implementation submitted for CI/review. Not a hardware-qualified RTOS timeline.** W17 was merged to `main` in PR #33 at `43f3e7f3aef84db91f89ffb727deab8e4ad2a193`.

## Data flow — no competing GDB connection

```text
W16 per-context C11 trace recorder
  -> W17 owned endpoint (transport supplies complete binary frames)
  -> W17 optional framed TCP capture client --spool NEW_FILE
  -> local append-only .rtob file (read only; no target access)
  -> W18 incremental LiveSession (frame/epoch/clock/uncertainty)
  -> W18 GUI: observed arrivals, per-lane dots, epoch and loss warnings
```

The `--spool` option is opt-in: it refuses an existing file, saves exact received bytes and flushes after a bounded socket read, on the existing Asio strand. GUI follows that file in bounded 16 KiB chunks while it is open, without opening a second debugger, probe socket or RTT client. A finite capture may fail or contain a partial trailing frame; **never** interpret a partial frame as a successful event.

Example with a **separately running, user-owned ordinary TCP trace endpoint**:

```powershell
# Build the optional TCP application first:
cmake -S . -B build-live -DOBSERVATORY_BUILD_LIVE=ON
cmake --build build-live --config Release

# In one terminal (an actual owned publisher is required):
.\build-live\rtos-observatory-framed-capture.exe --address 127.0.0.1 --port 12345 --spool capture.rtob

# Separately open the Observatory desktop W18 panel:
.\build-windows-clang\rtos-observatory-desktop.exe --live capture.rtob
```

`capture.rtob` must **not** already exist when capture begins. The GUI can be opened afterward, or with `File > Follow W18 binary trace spool...`; the panel includes an **Open / restart** button, an optional **Follow file** toggle and **Poll now**. It tracks one local spool at a time with an immutable active path independent of edits to the text field.

### Host session and evidence semantics

The C++20 `include/observatory/live_session.hpp` engine incrementally consumes W17 bytes. It **does not reconstruct omitted event content or silently merge epochs**.

- Each W17 publisher epoch has its own frame/event/stats counters. On epoch change, prior lane stats are cleared; reuse of an earlier epoch ID is **rejected** to prevent overlapping calibration or provenance. Decoder CRC faults/replay/unknown bytes and W17 sequence gaps remain visible.
- A default session allows at most **64 MiB input, 1,000,000 valid frames, 4,096 recent event records, 16 epochs**. After the recent event limit, the oldest **display** records are pruned, and history-truncation is labeled; source counters remain retained. Exceeding input/frame/epoch limits **poisons and halts** the instance; it cannot be silently reused.
- An incomplete trailing frame stays **waiting for bytes**, and `finish()` fails if the frame is incomplete. A finalized session cannot accept additional bytes.
- Every event retains raw `timestamp` ticks, `epoch`, `frame_sequence`, `lane`, `core_id`, `task_id`, `event_type`. **Task, ISR, mutex, scheduler, idle, and wait semantics are UNKNOWN** until qualified per-RTOS event mapping arrives. The GUI deliberately shows **arrival order only**, not a fabricated CPU load, task state or cross-core schedule. W16 event IDs have no canonical meaning yet.
- Clock mapping is **manual and epoch-scoped**: enter the board clock tick rate then click **Apply to current epoch**; the first observed tick becomes local zero microseconds. This is not automatic wall-time synchronization and remains unverified. The C++ conversion uses bounded, exact integer arithmetic; overflow/unknown calibration returns no time rather than a fake zero.
- `missing_frames` counts W17 wire sequence gaps, `missing_attempts` W16 lane attempt gaps. They **may overlap**; never sum them into total unique event loss. W16 full/busy telemetry remains independently sampled per epoch. A valid frame or clean EOF does **not prove that trailing events were delivered**.
- Local spools are **not authenticated**, cryptographically signed, or pinned to an on-disk inode; truncation is rejected, but same-size file replacement or a malicious source remains outside W18 attestation. No automatic network upload.
- The GUI polls only while open and opted in to Follow, remains single-threaded, and does not mix an incremental spool with existing W04 immutable offline trace sessions.

### Compatibility and bounded tests

A C/C++-compatible `firmware/obs_trace_types.h` now holds only POD event/record/stat types. C11 `atomic_uint` ring state remains in `obs_trace_ring.h` and `obs_recorder.h`. This prevents C11 atomic implementation details from leaking into W18 C++20 desktop or the optional W17 C++ Asio receiver, without changing W16 event binary fields.

- `observatory_live_epoch_model`: fragmented W17 event frames, epoch transitions, attempted-source/wire frame gap detection, cap/EOF/finalized refusal, calibrated and missing timestamps, and a synthetic `w18-synthetic.rtob` fixture.
- `observatory_desktop_live_smoke`: headless desktop import of the fixture (three events, two epochs).
- `observatory_imgui_live_ui_smoke`: hidden Linux Xvfb GLFW/OpenGL frame for the new panel, when available.
- Existing W17 `observatory_wire_crc_resync` and `observatory_wire_tcp_loopback` still gate wire framing. `OBSERVATORY_BUILD_LIVE=ON` and Windows clang-cl Release/Debug need actual CI and local tests.

**Not qualified:** live physical target trace, real UART/USB/RTT channel ownership and overhead, actual elapsed-time cross-core synchronization, CPU usage, timeline correctness for any Zephyr/FreeRTOS/TI-RTOS/ThreadX task, installed Eclipse plugin, or MSPM0G3507/XDS110. Those remain W19/W20+ or separate hardware qualification gates. No direct debugger control is allowed in W18.
