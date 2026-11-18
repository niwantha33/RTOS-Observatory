# Asio TCP capture foundation — W17/W18 partial

This opt-in host transport receives one finite `universal-v1` normalized trace over TCP. It uses standalone Asio 1.30.2 and one strand per session, and exercises two host worker threads. No Boost dependency or changes to the embedded C11 ring are required. Strands serialize callbacks; this host transport is not claimed to be lock-free or hard real-time.

## Build

The dependency-free offline core build remains the default. The optional TCP
capture now requires the exact **standalone Asio 1.30.2 Git submodule** under
`third_party/asio`; initialise it once before configuring:

```sh
git submodule update --init --recursive
cmake -S . -B build-live -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_LIVE=ON
cmake --build build-live --config Release
ctest --test-dir build-live -C Release --output-on-failure
```

CMake does not download Asio or accept an unverified globally installed
fallback. Asio is licensed under the Boost Software License 1.0; see
`third_party/asio/asio/LICENSE_1_0.txt`. The parent repository records its
exact Git commit; CI checks out the same source for Linux and Windows.

## Local demonstration

Start the synthetic sender in one terminal:

```sh
python tools/stream_trace.py --input examples/foundation.trace --port 12345
```

In another terminal:

```sh
./build-live/rtos-observatory-capture --address 127.0.0.1 --port 12345 --store tcp-session
./build-live/rtos-observatory --replay tcp-session --output tcp-report.json
```

For Visual Studio generators the executables are under `build-live/Release/` and end in `.exe`. Use a new session directory each time. The supplied example is synthetic, not measured target traffic.

## Wire and lifecycle contract

The source sends the existing universal-v1 header followed by six-column event rows and a final newline, then closes the connection cleanly. TCP chunks may split any row. CRLF and LF are accepted; raw bytes are preserved exactly. A connection is one epoch; there is no automatic reconnect or merge. Numeric IPv4/IPv6 addresses are accepted, with loopback as default. This is an outbound client; it opens no listening port, performs no DNS discovery and sends no target commands.

One 4096-byte read is outstanding. Buffering is bounded by raw bytes, partial-line length, normalized-event count, idle time and total duration. Default limits: 64 MiB, 4096-byte lines, 1 million normalized events, 5-second idle timeout, 30-second total timeout. The CLI exposes byte and time limits; the C++ API exposes all limits. TCP provides transport backpressure; buffer/duration overflow aborts rather than dropping silently. Sequence loss and explicit DROP retain the offline decoder's evidence semantics.

`TcpCapture::start()` and `stop()` post to the session strand. Duplicate starts/stops do not duplicate completion. Connect, read and timer handlers share that strand. Completion runs once after closing the socket and cancelling timers, and the caller must keep the io_context running until handlers drain. Completion handlers must not throw. Cancellation reports failure and never seals a successful capture. A generation counter prevents an obsolete idle timer completion from cancelling a refreshed session.

Each completed row is validated immediately. Only clean EOF with a final newline and a valid finalized decoder proceeds to immutable session storage. Invalid schemas, malformed rows, timeout, cancellation, connection errors or truncation do not create a session. Sequence gaps can produce a stored **incomplete** session (CLI exit 3); valid complete captures return 0; errors return 1. A failed capture's bounded partial bytes are returned to the API callback but the CLI does not persist them. CRC32 remains an accidental-corruption check, not authentication.

## Implemented and remaining

Implemented: async TCP receive, per-session strand, safe lifecycle cancellation, bounded assembly, raw preservation, validation/storage/replay handoff, synthetic loopback integration tests and multi-worker lifecycle tests.

This is a transport foundation, not complete W17/W18 or Phase 3. Rows are decoded during capture; diagnostic analysis runs after capture closes. Incremental diagnostic analysis, continuous disk spooling, graphical progress UI, device handshakes, serial/USB/RTT, packet CRC framing, TLS/authentication, vendor adapters, reconnect/reset negotiation and hardware rate/overhead qualification remain open. Use trusted local sources; remote TCP traffic is unauthenticated. A clean close does not prove that the sender recorded every target event.

## Incremental validation and progress — W18 partial

`UniversalStream` is an Asio-independent incremental parser. `feed()` accepts arbitrary byte fragments, retains only a bounded partial line plus the bounded normalized capture, and validates each complete row once. It checks the version/identity/clock header, row grammar, first SESSION, timestamps, sequences, explicit DROP counts and normalized-event limit (including inserted gap events). An error permanently invalidates the parser; create a new instance for another epoch. `finish(true)` requires the final newline for live capture. Offline decoding uses the same parser with `finish(false)` to retain acceptance of a complete final row without newline.

`TcpCaptureOptions::progress` receives a value snapshot on the session strand after each received chunk and on successful validation finalization. The snapshot includes received bytes, validated normalized event count, pending-line bytes, last validated timestamp, sequence gaps, declared drops and header/finalization flags. No callback is emitted for a chunk that fails validation. Callbacks should be short and nonblocking; slow callbacks delay reads and timers. A finalization flag means syntactic validation has finished, not that the evidence is lossless, analysis has completed or disk storage has succeeded.

The CLI emits flushed `Live events=... bytes=... timestamp_us=... missing=... drops=... finalized=...` progress lines while the connection is open. It limits normal updates to one per 200 ms when data arrives and emits the final snapshot regardless of that interval. There is no independent progress timer or GUI yet. Counts include synthetic gap DROP events; loss counters may overlap and must not be summed as an exact total. Raw bytes and normalized events remain bounded in memory; this is not continuous unbounded recording.

Both offline and incremental universal-v1 events now carry physical raw file line numbers (including header/comments) rather than positions in an intermediate normalized text buffer. Synthesized gap markers point to the following source row that proves the gap. Warning line references can therefore differ from older universal-v1 reports while identifying the original evidence more accurately; stored raw bytes and metadata formats are unchanged.

Tests cover every two-fragment split, one-byte feeds, CRLF, incomplete final rows, invalid/reset sequences, timestamps, bounds, poisoned parser reuse and known-answer report equivalence with an independently constructed normalized capture. Real loopback tests require progress before the sender closes and require malformed-row rejection while the sender remains connected. Hardware timing/rate remains unqualified.
