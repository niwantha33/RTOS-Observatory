# Phase 0 validation — 18 November 2026

Linux, GCC 14.2.0, CMake 4.4.5, C++20/C11.

| Configuration | CTest result |
| --- | --- |
| Debug | 6/6 passed |
| Release | 6/6 passed |

Tests cover the imported engine, SPSC target ring and saved-report adapter; CLI analysis; session storage/replay; identical capture/replay report bytes; unsupported schema/clock/decoder rejection; duplicate sequences; gap-induced incomplete analysis; byte/line/event limits; corrupt raw evidence, metadata and indexes; immutable session creation; invalid input cleanup; and CLI protection against writing inside sessions.

The universal fixture is synthetic. Real firmware timing, transport, Windows execution and multi-core causal correctness are not established by these tests. Windows/Linux Debug/Release CI is configured; consult its run results for platform qualification. Release builds retain the imported assertion tests.
