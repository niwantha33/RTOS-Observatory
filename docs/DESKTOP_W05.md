<!-- SPDX-License-Identifier: Apache-2.0 -->
# W05 — Standalone native desktop application

**Scope:** Phase 1 / W05. Standalone local Qt 6 Widgets UI using the existing C++20 core directly, without Eclipse or a new backend process. **Not** W06 CPU/timeline graphics, W07 causal graph, W08 time-travel replay, W14 GDB integration or real hardware live capture.

## User experience

- Dark native Windows/Linux window with sidebar, Overview, Events, Findings and Evidence details.
- Open a *normalized* Universal v1 or legacy TSV trace using the existing decoder registry, or open an indexed immutable Observatory session with full replay/integrity checks.
- Show task priorities/periods, event counts, observed wait/findings, data quality flags, loss counters and build/RTOS labels (not independent firmware attestation).
- Filter an in-memory **20,000-row event preview**, without rewriting source evidence. Input decoding remains limited by the core's existing byte/event bounds.
- Reopen up to 8 files/sessions *within the same application process*. No persistent recent-files database, network uploads, background polling or debugger ownership.
- Export a new JSON report without overwriting an existing file or modifying the session directory. On import failure, the previous valid report remains loaded.

## Build

Qt 6.5+ Widgets is an **optional** build dependency. The C++ CLI, C11 firmware ring and native core CI do not require Qt. Install a compatible Qt SDK (e.g. Ubuntu qt6-base-dev or Qt 6.8.3/MSVC2022 on Windows).

~~~sh
cmake -S . -B build-desktop -DOBSERVATORY_BUILD_DESKTOP=ON -DOBSERVATORY_BUILD_LIVE=OFF
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
~~~

Run the target \`rtos-observatory-desktop\` (for Visual Studio generators it may be in \`build-desktop/Release/\`). The UI supports:

~~~sh
rtos-observatory-desktop --trace examples/foundation.trace
rtos-observatory-desktop --session /path/to/indexed-session
~~~

\`--smoke examples/foundation.trace\` constructs the actual Qt MainWindow, runs the same trace loading path, checks the event table and exits. CI uses \`QT_QPA_PLATFORM=offscreen\`; interactive use requires a desktop environment.

## Acceptance and evidence

- Plain C++ \`SessionController\` invokes \`default_decoders\`, \`analyze\`, \`replay_session\` and \`report_json\` directly. Import is fail-closed without partially replacing a good loaded session.
- \`observatory_desktop_controller\` CTest covers trace/session import, decoder refusal, prior-state preservation, export, overwrite refusal and immutable-session protection.
- \`observatory_desktop_smoke\` CTest loads fixture through a genuine Qt window in headless mode.
- \`.github/workflows/desktop.yml\` builds and runs CTest on Windows and Linux in Debug/Release. Check the exact PR run before marking W05 done.
- The current output is source + executable when locally built with Qt. It is **not yet** a signed installer or redistributed portable Qt runtime. Audit Qt LGPL/commercial licensing, third-party notices, OS deployment dependencies and fresh-machine installation for a future release gate.

## Limitations

The existing synthetic fixture does not qualify target timing, live transport, actual RTOS vendor decoding, timeline accuracy, GPU performance or physical hardware. An offline finding is a recorded pattern, not proof of a missed deadline. The window does not open sockets, read GDB memory, claim to recover historical state or automatically alter firmware.

Phase: [#3](https://github.com/niwantha33/RTOS-Observatory/issues/3). Master: [#8](https://github.com/niwantha33/RTOS-Observatory/issues/8).
