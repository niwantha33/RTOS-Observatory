<!-- SPDX-License-Identifier: Apache-2.0 -->
# W17 — Versioned transport framing and bounded connection adapters

**Status:** software source and CI-test proposal. Physical UART/USB/RTT/target TCP drivers and rates are **NOT qualified**; hardware qualification deferred to W19. W17 is not a production live trace view (W18).

## W16 recorder → W17 transport

```text
Task/ISR instrumentation  [W16 C11 per-context SPSC rings]
                  |
         one consumer (not ISR)
                  |
    bounded 48-byte framed record + CRC32
                  |
      +-----------+--------------+--------------------+
   UART TX queue    USB CDC queue    Existing-owned TCP socket
   platform hook    platform hook    W17 Asio binary TCP receiver
      +------------------+
        qualified RTT* 
                  |
    host decoder: CRC, epoch, sequence, gaps, drops, duplicates
                  |
      W18 clock/session and incremental analyzer [planned]
```

*RTT requires the currently owning debugger/probe to explicitly qualify memory access and channel ownership; neither `obs_transport_v1.c` nor Observatory opens any RTT or OpenOCD connection. The project must not claim RTT safety from software tests alone.*

## Binary wire v1 — exact bytes

`firmware/obs_wire_v1.[ch]` implements a **little-endian**, independent-of-struct-layout binary protocol:

| Offset | Field | Bytes |
| --- | --- | ---: |
| 0 | Magic `RTOB` | 4 |
| 4 | Protocol version = `1` | 1 |
| 5 | Type: event `1`, stats `2` | 1 |
| 6 | Payload length LE (event 28, stats 24) | 2 |
| 8 | Publisher-provided nonzero epoch LE | 4 |
| 12 | Global transport frame sequence LE, starts at 1 each epoch | 4 |
| 16 | Event or stats payload | 24/28 |
| End | CRC32/IEEE of header + payload, LE | 4 |

**Total frame sizes:** event = **48 bytes**, stats = **44 bytes**. Maximum receive buffer in the C11 decoder = **48 bytes**. Decoder accepts arbitrary byte fragments and resynchronizes after bad headers/CRC; it rejects unknown schema, unsafe lengths, zero epoch/sequence, per-epoch replayed transport frames and duplicate/regressing per-lane attempts.

**Event payload (28 bytes):** `timestamp:uint64`, `task_id:uint32`, `object_id:uint32`, `event_type:uint16`, `core_id:uint16`, `lane_attempt:uint32`, `lane:uint16`, `recorder_schema_version:uint16`. This remains caller-defined clock ticks; **not** automatically nanoseconds or synchronized across cores.

**Stats payload (24 bytes):** `lane:uint16`, reserved zero `uint16`, W16 `attempted:uint32`, `accepted:uint32`, `full_drops:uint32`, `busy_drops:uint32`, `pending:uint32`. Counters are **noncoherent** snapshots and may wrap. The host reports missing transport frames separately from W16 attempt gaps; do not add the two together because they may overlap.

A sequence gap is reported only **when a later valid frame exposes it**. An unreported final tail loss cannot be proved absent. Late attachment sets initial unknown/missing history; a changed epoch is explicitly flagged, not silently joined into one timeline. Counter overflow saturates the reported loss totals with a flag. Sequence exhaustion needs a new epoch, not an implicit wrap. **CRC is error detection, not authentication or encryption**.

## Sender ownership and backpressure

`firmware/obs_transport_v1.[ch]` provides an allocation-free, single-consumer transmitter with exactly **one staged frame**. A user-provided `obs_port_submit_t` callback must accept/enqueue the **whole** frame atomically or return `OBS_PORT_RETRY` without consuming any bytes. Never pass a socket `write_some` or partially accepting UART/USB callback directly; wrap it in a bounded, protocol-compliant platform TX queue.

- `ACCEPTED`: complete frame accepted by local sender queue; **not** delivery/acknowledgment.
- `RETRY`: retain frame, do not pop another W16 record; increments backpressure count.
- `FAILED`: explicitly count a lost frame, release staged storage; the next successfully transmitted transport sequence reveals the gap.
- The sink is one consumer, not an ISR, and must not block. Status frames must be emitted periodically by the consumer through `obs_transport_emit_stats`; export local sender backpressure/failure counters in a future diagnostics channel.
- UART, USB CDC, owned TCP and RTT are selectable interface *contracts*, not verified hardware drivers. `OBS_PORT_RTT` additionally requires explicit `rtt_qualified=true` and endpoint ownership; default RTT init fails closed.
- Epoch must be new after reconnect. A callback incorrectly reporting acceptance after a partial send is **outside the contract** and can desynchronize receivers.

## Optional real loopback TCP receiver

Under `OBSERVATORY_BUILD_LIVE=ON`, W17 adds a separate client executable `rtos-observatory-framed-capture` using the existing standalone Asio library. It connects only to an **ordinary, already owned** numeric TCP address, not to GDB, OpenOCD or an RTT port. One Asio strand, bounded bytes/frames and finite idle/total timeouts; a capture is successful only on clean EOF with at least one CRC-valid frame. Incomplete/truncated and CRC/replay/gap cases are surfaced, not guessed.

```sh
cmake -S . -B build-live -DOBSERVATORY_BUILD_LIVE=ON
cmake --build build-live --config Release
ctest --test-dir build-live -C Release --output-on-failure
./build-live/rtos-observatory-framed-capture --address 127.0.0.1 --port 12345
```

CLI returns `0` for clean EOF with no **observed** loss, `3` for clean EOF with observed gaps/corruption or epoch transitions, and `1` for connection errors, timeout, truncation, empty/no valid capture or byte/frame limit violation. **0 is not an end-to-end, lossless capture certificate:** trailing loss might be unseen and physical transport/target timing are unqualified. The executable counts metadata but does not yet persist a W18 fully replayable session; it is intended for finite qualification and live protocol validation.

## Software tests and acceptance

- `observatory_wire_crc_resync`: fixed independent LE/CRC32 golden byte vector, fragmentation, malformed length/type/version, CRC corruption, late attachment, sequence gaps, duplicate/replayed frames, stats and epoch restart.
- `observatory_wire_nonblocking_sink`: in-memory mock UART/USB/owned-TCP/qualified-RTT sink, full-frame RETRY, pending preservation, FAILED accounting, per-lane and frame gaps, explicit epoch exhaustion.
- `observatory_wire_tcp_loopback` (**only with live build enabled**): real local TCP connection, 3-byte fragmentation, valid event+stats, CRC corruption/recovery, replay, epoch transition, timeouts, byte/frame budgets and incomplete EOF.
- Native Windows/Linux Debug+Release, Windows clang-cl, optional Asio build and review are the software gates. No hardware throughput or RTOS ISR safety proof is claimed.

**Outstanding before marking W17 complete:** actual target-specific UART/USB/RTT/network adapters, safe TX queue ownership and timeout recovery, production host connectors, instrumented W16/target overhead, trace replay equivalence and W19 physical qualification. W18 implements continuous capture/clock mapping/epoch coordination; there is no automatic refresh into the current desktop timeline.
