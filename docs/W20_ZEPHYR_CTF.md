<!-- SPDX-License-Identifier: Apache-2.0 -->
# W20: Zephyr CTF native desktop, evidence-first preview

This is a **public, generic Zephyr CTF adapter** for the native C++ Dear ImGui desktop. Private algorithms, user captures, generated trace reports, build artefacts, ELFs and private roadmap material are excluded from the repository.

## Windows local test

Build normally from `main` **after PR merge**, or switch to `feature/w20-zephyr-ctf-native-gui` to test before merge. The local input must be a directory with the two files `metadata` and `channel0_0`; the user's files must remain **outside the Git checkout**.

```powershell
cmake -S . -B build-w20 -DOBSERVATORY_BUILD_DESKTOP=ON
cmake --build build-w20 --config Release
.\build-w20\rtos-observatory-desktop.exe --ctf "C:\PrivateTraces\ZephyrCapture"
```

On multi-config CMake generators the executable may instead be inside `build-w20\Release`. You can also double-click `tools/run-w20-gui.bat`, select **Zephyr CTF / W20** in the compact sidebar and click **Browse folders...**. On Windows this opens the native system folder chooser. **File → Open Zephyr CTF folder...** also opens the chooser. Choose the folder containing `metadata` and `channel0_0` (or its immediate parent if it contains exactly one CTF capture folder). The panel displays a specific error for a missing file; it never searches arbitrary locations or uploads the recording. Dragging a CTF directory onto the GUI is also supported.

## Included

- Version- and layout-checked CTF 1.8 little-endian decoder for a common Zephyr unpacketized header (32-bit clock ticks and 16-bit event ID).
- Metadata-defined event payloads with strict bounded types/lengths, timestamp rollover checks, malformed/truncated input refusal, bounded sizes and event counts.
- Separate W20 desktop page with **mutex, semaphore and message-queue blocking/return correlations**, raw event/byte offsets, observed thread identity, wait timeline and clickable source evidence.
- Optional **Follow local file** polls a growing local channel; a partial trailing record is not accepted. This does not connect to a physical target or prove loss-free streaming.
- Optional second capture to inspect *descriptive* before/after counts. No automatically inferred performance gain; captures must be independently matched and calibrated.
- Synthetic-only native parser and GUI smoke tests in Windows/Linux CI.

## Limits / not qualified

Raw ticks are **not microseconds**, CPU utilization is unknown, and no total event-loss guarantee is available. Causal relationships are investigation hypotheses, **not proof** of an RTOS bug or priority inheritance. This decoder refuses CTF packet/context layouts outside the supported subset. No physical hardware, UART/RTT/GDB ownership, cross-core clock calibration or production real-time qualification is claimed.

Do not upload a real CTF capture or reports to public GitHub issues, PRs or CI logs. Run them privately on the workstation.


## W20 two-column investigation UI

The **left explorer** has four tabs: Cases (filter All / Mutex / Semaphore / Queue),
Timeline (non-overlapping raw-tick tracks), Events (bounded searchable event list),
and Compare (second independent local capture). Selecting a case or event updates
the **persistent right evidence inspector**, which displays callback assessment,
linked records, raw ticks, byte offsets and recorded field values.

For narrower windows the explorer and inspector stack vertically. The main
navigation groups normalized-trace tools separately so those offline views
cannot be mistaken for CTF findings. The native file picker does not upload
the capture; the UI makes no unverified CPU load or causal claims.
