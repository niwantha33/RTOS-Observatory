<!-- SPDX-License-Identifier: Apache-2.0 -->
# W16 — Bounded C11 multi-context firmware trace recorder

**Status:** software implementation submitted, host qualification pending. **Not** a qualified MCU/RTOS trace driver, transport, or a measured overhead result. No hardware tests have run; W19 is the deferred hardware qualification gate.

## Data flow

```text
[Task / ISR / per-core producer 0] -> [dedicated SPSC lane 0] ---+
[Task / ISR / per-core producer 1] -> [dedicated SPSC lane 1] ---+--> [one drain context]
[Task / ISR / per-core producer 2] -> [dedicated SPSC lane 2] ---+          |
[Task / ISR / per-core producer 3] -> [dedicated SPSC lane 3] ---+       future W17
                    |                                    |          framing/transport
           FULL and BUSY counters             sequence gaps
```

The previous `firmware/obs_trace_ring.[ch]` stays unchanged. New `firmware/obs_recorder.[ch]` is an **additive**, static-allocation recorder. Each lane permits one writer and one separate reader: intended writers must be assigned stable unique lane IDs and **must not** accidentally share a lane between tasks or interrupt contexts. The `atomic_flag` prevents simultaneous writers on a lane from corrupting memory, but a contending or nested caller is **dropped (BUSY)**, not serialized. This is not a general MPMC queue. The drain reader must be a **single** consumer, e.g. a low-priority thread; calling `obs_recorder_pop` from multiple consumers is unsupported.

### Event data and loss

- Every accepted event includes caller-supplied timestamp, task ID, object ID, event type, core ID, recorder **schema version 1**, producing lane and **per-lane attempt sequence**.
- On **FULL**: discard the new event, increment `full_drops`; consume an attempt sequence number, making gaps visible when later events are accepted. The recorder never overwrites unread events.
- On **BUSY**: discard nested/competing emission, increment `busy_drops`; there is no sequence assignment for an unordered rejected contender. A decoder must use **both** sequences and counter deltas.
- `obs_recorder_stats` exposes `attempted`, `accepted`, `full_drops`, `busy_drops`, and approximate `pending`, all per lane. These counters are individually atomic, **not a coherent multi-field snapshot**. They wrap modulo the native unsigned width and need W17/W18 epoch/wrap handling.
- Per-lane FIFO is preserved. **There is no global ordering or timestamp/clock synchronization across lanes or CPUs.** Timestamp source is supplied by the user firmware. W18 will specify clock mappings and session epochs. **Record struct memory layout is not a wire format**; W17 must use explicit versioned serialization, framing and CRC/loss delivery.

### Embedded footprint / ISR contract

Compile-time defaults: `OBS_RECORDER_MAX_LANES=4`, `OBS_RECORDER_CAPACITY=64`, 63 usable events per lane (power of two), no `malloc`, allocator, printf, I/O, callbacks, mutex, strand, spin wait or GDB call in the hot path. Total resident recorder storage is `sizeof(obs_recorder_t)`; adjust both macros **for the board/RTOS**, building the firmware object and application with identical definitions. Initialization is once, before publishing the recorder and enabling hooks; do **not** reset live state. Check `obs_recorder_init` for target-supported lock-free atomics. An ISR integration must still check architecture-specific preemption, interrupt priorities, atomic implementation, memory ordering, RAM placement and **measured WCET**; host lock-free qualification does not prove MCU worst-case latency.

Minimal example (all producer and consumer contexts are set up by the RTOS-specific adapter):

```c
#include "obs_recorder.h"
static obs_recorder_t trace;

void early_init(void) {
    if (!obs_recorder_init(&trace, 2u)) {
        /* Disable tracing and report unsupported atomics to a safe channel. */
    }
}
void on_task_event(uint64_t ticks, uint32_t task_id) {
    obs_trace_record_t event={ticks,task_id,0u,1u,0u};
    (void)obs_recorder_emit(&trace,0u,&event); /* single writer for lane 0 */
}
void on_isr_event(uint64_t ticks, uint32_t irq_id) {
    obs_trace_record_t event={ticks,0u,irq_id,2u,0u};
    (void)obs_recorder_emit(&trace,1u,&event); /* single writer for lane 1 */
}
void drain_one(void) {
    obs_recorder_record_t r;
    if (obs_recorder_pop(&trace,&r)) {
        /* W17 transport will serialize + transmit; absent in W16. */
    }
}
```

**Important:** The example is illustrative only. A target with multiple ISR preemption levels may need **more than one ISR lane**, and verified constant-time atomic operations. In a real RTOS the hook must never change scheduler ownership or perform blocking I/O. Instrumentation disabled/failure must not cause an RTOS fault. The returned status and counters must not be ignored by a production telemetry strategy; W17 must export them.

## Software qualification

- `observatory_recorder_bounded`: invalid input, initialization, full buffer, ring wrapping, independent lanes, fair drain, overflow sequence gap, nested writer rejected as BUSY, exact loss counters.
- `observatory_recorder_concurrency`: two simultaneous **real host threads** writing separate lanes, single consumer, per-lane monotonic sequence, payload integrity, accepted/full/attempt accounting. Windows uses Win32 threads; POSIX uses pthreads. Host races are exercised, not all MCU preemption cases.
- Native Windows/Linux Release+Debug and Windows clang-cl Release+Debug CI still required; PR result must be green before merge.
- The original `observatory_ring_tests` remains in CTest; the existing SPSC library contract is unchanged.
- **Hardware qualification deferred:** MSPM0G3507/XDS110, TI-RTOS, Zephyr and FreeRTOS preemption tests, RAM usage, per-event host/target throughput, event loss at scale, ISR WCET, and clock accuracy. No MHz, event/s, CPU%, microsecond latency or lock-free portability claims are made without evidence.

**W16 stops at the in-memory recorder and local consumer.** W17 transport/framing, W18 live clock/session coordinator and W19 board qualification are separate packages. 
