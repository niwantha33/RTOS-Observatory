# Phase 0 foundation (0.2.0)

The standalone C++20 engine and C11 target ring require no Eclipse, Node, database server or downloaded build dependencies. CMake 3.20+ and a C++20 compiler are required. Python enables adapter and CLI integration tests. Apache-2.0 source was imported from the existing 0.1.0 prototype; its limitations remain in force.

## Work packages

| ID | Delivered | Qualification |
| --- | --- | --- |
| W01 | Prototype import, CMake, CTest, Linux/Windows Debug/Release CI | Linux verified locally; Windows requires CI result |
| W02 | Versioned universal-v1 TSV contract, task/ISR/mutex and semaphore/queue wait families, clock and evidence metadata | Normalized offline data; generic semaphore/queue waits only |
| W03 | Decoder interface/registry, legacy and v1 decoders, byte/line/event bounds, sequence gaps and explicit drops | In-process trusted plugins; no live RTOS decoder |
| W04 | Immutable session directories, original raw bytes, row offset/timestamp index, firmware identity, metadata, deterministic replay and CRC32 corruption check | Local single-writer storage; no authentication or database query API |

## Repeatable exit gate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/rtos-observatory --decoder universal-v1 --input examples/foundation.trace --store capture-session --output captured-report.json
./build/rtos-observatory --replay capture-session --output replayed-report.json
```

On Visual Studio generators use `build/Release/rtos-observatory.exe`. The two reports must be byte-identical. Use a new session directory for each capture. The CLI refuses output inside a session and input/output path aliases. CTest includes automated round-trip verification.

## Ingestion architecture

`DecoderRegistry` resolves an explicit decoder ID; no guessing by filename. A decoder receives bounded input and returns a `DecodedTrace` with a capture, firmware/RTOS identity, clock and loss evidence. Registration rejects duplicate IDs and null decoders. A future vendor adapter must validate native metadata and normalize time and priorities before returning these events. Plugins are trusted compiled code, not a sandbox.

Default ingestion bounds are 64 MiB raw bytes, 4096 bytes per line and 1,000,000 normalized events including inserted gap markers. Limits are configurable in the C++ API. Input is buffered within these caps: this foundation is bounded offline processing, not a constant-memory streaming transport. Existing `parse_trace` remains available with its own line/event bounds.

## Session format v1

Each session contains:

* `raw.trace`: exact original bytes, including comments and line endings.
* `metadata.tsv`: schema version, decoder ID, firmware build ID, RTOS ID, clock ID/frequency, raw size/CRC32, session ID/event count, and sequence/drop counters.
* `index.tsv`: schema version and one row per raw event: physical line, byte offset, timestamp in microseconds. Header/comments are excluded. Gap markers exist only in the normalized capture.

A session reserves a new directory and writes metadata last. Handled write failures remove the partial directory; interruption can leave a partial directory which replay rejects. No overwrite, repair, retention manager, fsync power-loss guarantee or cross-process reader/writer locking is provided. Publish completed sessions to other readers after the writer finishes.

Replay bounds and validates every file, decodes raw evidence afresh and requires exact metadata/index agreement. CRC32 detects ordinary raw-byte corruption; it is not a cryptographic signature, and firmware identity is a supplied build label, not verified attestation. A person who rewrites all artifacts can forge a session. Legacy captures explicitly carry unknown firmware/RTOS and unavailable sequence coverage. A gap before the first recorded sequence or after the last cannot be inferred.

The index is persisted for navigation; this phase replays the whole bounded capture. Range-query and seek APIs belong to a later storage phase. Relative paths in traces never control filesystem operations.

## Deliberate boundaries

The normalized model records task lifecycle/priority/cycles, nested ISR intervals by core, mutex ownership/waits, and typed semaphore/queue waits. It does not invent ISR identity, queue depth/payload, semaphore counts, Rust runtime tasks, raw CTF/UIA decoding, live capture or multi-core causal qualification. The clock schema requires normalized microseconds; adapters must handle original ticks, wraps and resets before emission.
