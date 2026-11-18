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

Versioned dependency Git tags are used by CMake FetchContent. First configuration
requires internet access or local sources via FETCHCONTENT_SOURCE_DIR_IMGUI and
FETCHCONTENT_SOURCE_DIR_GLFW. The core-only build never downloads UI dependencies.

## First Windows build

Open x64 Native Tools Command Prompt (VS 2022/2026), or activate the Visual
Studio developer environment in PowerShell so cl, rc and mt from Windows SDK
are available. clang-cl by itself does not supply Windows SDK imports/manifest
tools. Build from the repository:

    git fetch origin
    git switch --track origin/feature/dear-imgui-desktop-w05-w06
    cmake -S . -B build-imgui -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_DESKTOP=ON
    cmake --build build-imgui --parallel 2
    ctest --test-dir build-imgui --output-on-failure
    .\build-imgui\rtos-observatory-desktop.exe --trace .\examples\timeline.trace

If the branch exists locally, switch to it and pull --ff-only. Use a fresh
build directory when changing generator/compiler. You may omit Clang flags
to use MSVC from the developer shell.

## Ubuntu / Linux

Install Ninja, CMake, a C++20 compiler, GLFW/OpenGL/X11 development libraries
and Xvfb for headless UI tests:

    sudo apt-get install cmake ninja-build g++ git libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev xvfb
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
