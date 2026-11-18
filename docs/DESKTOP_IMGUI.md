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
- No queue-depth reconstruction, inferred ready or sleeping state, debug
  probe access, network listener or raw native Zephyr CTF decoder.

## Validation

- Controller CTest covers import integrity and safe export.
- Desktop --smoke fixtures cover real core analysis without a graphics context.
- Linux --smoke-ui runs one actual hidden GLFW/OpenGL+Dear ImGui frame under
  xvfb-run in CI; Windows/Linux Debug+Release CI must also pass.
- Synthetic data cannot validate hardware timing or production live capture.
- The historic Qt W05/W06 merged PRs remain recorded for audit; this branch
  is the new intended implementation until reviewed and merged.
