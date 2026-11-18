# Asio TCP capture foundation — W17/W18 partial

This opt-in host transport receives one finite `universal-v1` normalized trace over TCP. It uses standalone Asio 1.30.2 and one strand per session, and exercises two host worker threads. No Boost dependency or changes to the embedded C11 ring are required. Strands serialize callbacks; this host transport is not claimed to be lock-free or hard real-time.

## Build

Keep the dependency-free offline build with the default CMake options. For TCP capture, provide a standalone Asio include path:

```sh
cmake -S . -B build-live -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_LIVE=ON -DOBSERVATORY_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-live --config Release
ctest --test-dir build-live -C Release --output-on-failure
```

Alternatively explicitly set `-DOBSERVATORY_FETCH_ASIO=ON`; CMake then downloads the 1.30.2 archive with a pinned SHA256. There is no automatic network fetch in the default build. Asio is licensed under the Boost Software License 1.0; see the downloaded archive's `asio/LICENSE_1_0.txt`. An externally supplied include path is caller-managed and is not digest-verified by CMake. CI exercises the pinned archive on Linux and Windows.

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

One 4096-byte read is outstanding. Buffering is bounded by raw bytes, partial-line length, data-row count, idle time and total duration. Default limits: 64 MiB, 4096-byte lines, 1 million normalized events, 5-second idle timeout, 30-second total timeout. The CLI exposes byte and time limits; the C++ API exposes all limits. TCP provides transport backpressure; buffer/duration overflow aborts rather than dropping silently. Sequence loss and explicit DROP retain the offline decoder's evidence semantics.

`TcpCapture::start()` and `stop()` post to the session strand. Duplicate starts/stops do not duplicate completion. Connect, read and timer handlers share that strand. Completion runs once after closing the socket and cancelling timers, and the caller must keep the io_context running until handlers drain. Completion handlers must not throw. Cancellation reports failure and never seals a successful capture. A generation counter prevents an obsolete idle timer completion from cancelling a refreshed session.

Only clean EOF with a final newline proceeds to decoder validation and immutable session storage. Invalid schemas, malformed rows, timeout, cancellation, connection errors or truncation do not create a session. Sequence gaps can produce a stored **incomplete** session (CLI exit 3); valid complete captures return 0; errors return 1. A failed capture's bounded partial bytes are returned to the API callback but the CLI does not persist them. CRC32 remains an accidental-corruption check, not authentication.

## Implemented and remaining

Implemented: async TCP receive, per-session strand, safe lifecycle cancellation, bounded assembly, raw preservation, validation/storage/replay handoff, synthetic loopback integration tests and multi-worker lifecycle tests.

This is a transport foundation, not complete W17/W18 or Phase 3. Parsing and analysis run after capture closes. Incremental decoder/analysis updates, continuous disk spooling, progress UI, device handshakes, serial/USB/RTT, packet CRC framing, TLS/authentication, vendor adapters, reconnect/reset negotiation and hardware rate/overhead qualification remain open. Use trusted local sources; remote TCP traffic is unauthenticated. A clean close does not prove that the sender recorded every target event.
