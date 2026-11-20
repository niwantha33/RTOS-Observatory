<!-- SPDX-License-Identifier: Apache-2.0 -->
# W05/W06 — Dear ImGui native desktop migration

RTOS Observatory now targets an independent Dear ImGui + GLFW + OpenGL3
desktop, retaining the in-process C++20 SessionController and evidence-safe
W06 model. The core library, CLI and C11 target ring remain Qt-free.

## Licence and dependencies

- Dear ImGui v1.92.6: MIT.
- GLFW 3.5.1: zlib/libpng.
- RTOS Observatory: Apache-2.0.
- Include third-party copyright and licence notices when distributing binaries.
- OpenGL / platform drivers and SDK components remain required.
- No Qt SDK, Qt DLLs, or Qt paid licence is needed for this backend.

All three external source libraries are now **Git submodules with exact commit IDs**
recorded as gitlinks. ImGui and GLFW are consumed from third_party/; standalone
Asio is pinned for the optional live transport. No CMake FetchContent, system
library fallback or configure-time library download is used. Initialise them
once with `git submodule update --init --recursive`; subsequent offline CMake
configurations can work without the network. Git still requires access to
those upstream repositories on the first submodule checkout. Missing submodule
contents cause a clear configure error.

## First Windows build (tested local clang-cl / MSVC SDK combination)

Install Visual Studio C++ build tools + Windows 10/11 SDK, LLVM clang-cl/lld-link,
CMake and Ninja. Open ordinary PowerShell; no VS developer prompt is necessary.
Initialize the pinned git submodules, then run the repository's **versioned**
Windows build script. It locates MSVC x64 libraries, Windows SDK headers/lib/rc/mt,
filters out non-version folders such as SDK Lib/wdf, and uses forward-slash CMake
compiler paths. It does not invoke the developer shell script or change system
environment/registry settings.

    Set-Location C:\My-Projects\RTOS-Observatory
    git submodule sync --recursive
    git submodule update --init --recursive
    .\tools\build-windows-clang.ps1 -Configuration Release
    .\build-windows-clang\rtos-observatory-desktop.exe --trace .\examples\timeline.trace

For Debug, use a **separate** build folder to avoid reusing the Release cache:

    .\tools\build-windows-clang.ps1 -Configuration Debug -BuildDirectory build-windows-clang-debug

A green SUCCESS is printed only if CMake configuration, the full desktop build
and CTest all finish with zero exit code. The command writes
build-windows-clang/windows-clang-build.log for diagnostics. If it fails, inspect
that file and repair missing MSVC/Windows SDK components using Visual Studio
Installer. Do not bypass CMake compiler checks or use a different CRT.

GitHub Actions verifies the same script in Windows Debug and Release separately
from the existing MSVC/Ubuntu desktop jobs. Existing manually configured build
folders must not be reused with a different compiler or generator.

## Ubuntu / Linux

Install Ninja, CMake, a C++20 compiler, GLFW/OpenGL/X11 development libraries
and Xvfb for headless UI tests:

    sudo apt-get install cmake ninja-build g++ git libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev xvfb
    git submodule update --init --recursive
    cmake -S . -B build-imgui -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_DESKTOP=ON
    cmake --build build-imgui --parallel 2
    ctest --test-dir build-imgui --output-on-failure
    ./build-imgui/rtos-observatory-desktop --trace examples/timeline.trace

## Features

- Open normalized traces and immutable indexed sessions by entering a path
  or dragging and dropping it onto the window; retain Universal-v1/legacy
  decoder selection.
- Overview, bounded 20k-row event search, findings, evidence quality and
  safe non-overwriting JSON export; last eight paths are process-local.
- Timeline shows explicit CPU, task, ISR, idle, unknown and completed-wait
  lanes with zoom/fit, exact microsecond window and hover details.
- CPU / waits shows separate per-core running/ISR/idle/unknown durations,
  gated CPU percentages and completed wait summaries.
- Native Zephyr CTF has a separate evidence-first adapter with original raw-tick
  timeline bars, resource callback intervals and source-event inspection.
  It does **not** claim clock-calibrated CPU utilization, ownership or causal
  inference, loss-free capture, live probe access or queue-depth reconstruction.

## Responsive adapter-first workspace

Data sources now open a **Connect data source** import dialog. Zephyr CTF,
universal trace, saved sessions and binary event spools share a single menu of
analysis tools. The active source determines which conclusions are supported.
The user can change text/UI scale from Settings; the window layout switches
from sidebar to a tool selector on narrow displays. Details and manual
acceptance checks are in [UI_ADAPTER_WORKBENCH.md](UI_ADAPTER_WORKBENCH.md).

## Validation

- Controller CTest covers import integrity and safe export.
- Desktop --smoke fixtures cover real core analysis without a graphics context.
- Linux --smoke-ui runs one actual hidden GLFW/OpenGL+Dear ImGui frame under
  xvfb-run in CI; Windows/Linux Debug+Release CI must also pass.
- Synthetic data cannot validate hardware timing or production live capture.
- The historic Qt W05/W06 PRs remain recorded for audit. The Dear ImGui migration
  (PR #14) and application shell/metadata (PR #16) are merged on main with CI.

## Desktop application shell and build metadata

The **File, Settings, Window, Tools, Help** menu bar is available on Windows and
Linux. File provides local path-based trace/session opening, process-local recent
files, non-overwriting JSON export and Exit. Settings controls decoder selection
and compact spacing; Window switches views, shows or hides the navigation panel
and restores its default view; Tools provides timeline fit/zoom and CPU/evidence
shortcuts. Help → About displays version, source commit and UTC configuration time.

The title bar and footer show the configured project version. To inspect the exact
embedded metadata without launching graphics:

    .\\build-windows-clang\\rtos-observatory-desktop.exe --version

The commit is taken from the checked-out Git HEAD when Git is available, otherwise
shown as `unknown`. The time is the **UTC CMake configure time**, not a claim
about when compilation or signing finished. Reconfigure CMake after changing the
checkout to refresh metadata. No user settings or recent evidence paths are saved
between process runs. Dialogs accept typed local paths or the window accepts drops;
there is not yet a native operating-system file picker.

The dedicated `observatory_desktop_build_info` CTest validates `--version` output,
while existing controller, model, timeline and graphical smoke tests remain in CI.

## UI typography and readability (v0.2.1)

The desktop uses a 17-pixel regular UI font (scaled once for the display's
content scale at window creation) and more legible spacing, contrast, section
headings and labels. It prefers a **locally installed Inter Regular** font. If
Inter is unavailable, Windows uses Segoe UI; Linux checks Noto Sans, DejaVu Sans
and Liberation Sans; ImGui's built-in font is the last fallback at 17 px.

No font downloads, font binary redistribution, online initialization or extra
build dependency are introduced. To use a custom accessible TTF/OTF font, set
`OBSERVATORY_UI_FONT` to its file path before launching the desktop.
Install Inter locally if the Inter appearance is desired across machines.
The font is selected at launch; dynamic font rebuilding after moving between
monitors of different DPI is not yet supported. Running the desktop directly with `--smoke-ui examples/timeline.trace` prints
the selected font name. CTest hides successful test stdout in normal CI logs.

Navigation, menu actions, decoder selection, evidence warnings and report
integrity remain intact. CPU metrics and unknown periods retain their original
evidence rules. This is a readability iteration, not W07/W08 or live debugging.


## Professional workspace presets (Step 1, opt-in local layout)

- Default typography is a compact 85% UI zoom with compact spacing. Settings offers
  75%, 85%, 100%, 120%, 150%, 180% presets, a fine slider and Ctrl+Plus/Minus.
  GLFW/ImGui recompute scale on resize, DPI movement and detached viewports.
- Window > Detach current analysis / Detach resource explorer / Detach evidence
  inspector / Detach scheduling timeline are native dockable windows.
- Window > **Save layout locally** stores font zoom, navigation, detached-tool
  visibility and a tightly allowlisted subset of ImGui geometry in the user's
  local configuration directory. **Restore saved layout**, **Forget saved layout**
  and **Reset layout** are explicit actions. Saving is never automatic.
- On Windows the file lives under LOCALAPPDATA/RTOS-Observatory/workspace.layout;
  on Linux, under XDG_CONFIG_HOME/rtos-observatory/workspace.layout (or
  ~/.config/rtos-observatory/workspace.layout).
- Trace paths, captures, event values, session identities and table content are
  never serialized. Only fixed application window names and docking geometry
  pass the load/save filter. Corrupt or oversized files are rejected.
- Saved floating-window coordinates must be manually checked on mixed-DPI,
  disconnected monitors and 4K displays; **Reset layout** is the recovery path.
  No physical multi-monitor testing is claimed by CI.

## CTF timeline: zoom, ruler and display-only time conversion

- Shared Timeline, CPU/scheduling and detached Timeline use one tick-offset viewport.
  Zoom in/out, Fit all; Ctrl+wheel zooms at cursor; Shift+wheel and middle-drag
  pan, and the horizontal position slider supports precise navigation.
  Focus finding zooms to a selected evidence case.
- Ruler labels show elapsed time relative to original capture start. Floating
  point is used only for pixel offsets; raw source ticks remain unsigned 64-bit.
- Time units: Auto, source, us, ms, seconds. Normalized trace unit is us.
  Zephyr CTF stays in raw ticks until a user explicitly selects known clock Hz
  and enters the actual frequency for that capture. Converted time is always
  marked a MANUAL ESTIMATE and never changes clock_calibrated or loss_qualified.
- Zoom persists across live append, resets on source change; off-screen spans
  are clipped. 80-lane and preview caps remain visible, not hidden.
- The standalone C++ test covers long timestamps, zoom, pan, focus, growth
  retention and clock conversions. Mixed-DPI interactions still need manual
  verification on Windows/Linux hardware.
