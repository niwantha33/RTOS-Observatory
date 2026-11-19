// SPDX-License-Identifier: Apache-2.0
#ifndef OBS_RECORDER_H
#define OBS_RECORDER_H
#include "obs_trace_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static, allocation-free, no I/O: separate SPSC ring per producer context.
// Never share a *consumer* across tasks/ISRs. A lane has ONE intended writer;
// accidental nested/competing writers are rejected without blocking.
// Tune these at compile time for each MCU's RAM budget.
#ifndef OBS_RECORDER_MAX_LANES
#define OBS_RECORDER_MAX_LANES 4u
#endif
#ifndef OBS_RECORDER_CAPACITY
#define OBS_RECORDER_CAPACITY 64u
#endif
#define OBS_RECORDER_USABLE (OBS_RECORDER_CAPACITY - 1u)

typedef enum {
    OBS_RECORDER_OK = 0,
    OBS_RECORDER_FULL = 1,
    OBS_RECORDER_BUSY = 2,
    OBS_RECORDER_INVALID = 3
} obs_recorder_result_t;

typedef struct {
    // Only init before the recorder is published; never reset a live lane.
    atomic_uint head;                // producer-owned, consumer reads
    atomic_uint tail;                // consumer-owned, producer reads
    atomic_uint attempts;            // acquired attempts, wraps after UINT_MAX
    atomic_uint accepted;            // successful enqueues, wraps after UINT_MAX
    atomic_uint full_drops;          // ring full, wraps after UINT_MAX
    atomic_uint busy_drops;          // reentry/contending writer, wraps after UINT_MAX
    atomic_flag writer_active;       // reject reentry, do not spin/block in ISR
    obs_recorder_record_t buffer[OBS_RECORDER_CAPACITY];
} obs_recorder_lane_t;

typedef struct obs_recorder_t {
    // Both ready and lane_count are immutable after successful init.
    bool ready;
    unsigned lane_count;
    unsigned consumer_cursor;        // one consumer owns this field
    obs_recorder_lane_t lanes[OBS_RECORDER_MAX_LANES];
} obs_recorder_t;

// Runtime capability check: no lock-based fallback for head/tail/telemetry.
// The caller MUST initialize before enabling any task/ISR instrumentation.
bool obs_recorder_init(obs_recorder_t *recorder, unsigned lane_count);

// Safe in an ISR ONLY after platform qualification of C11 lock-free atomics
// and their worst-case timing. No allocator, callback, locks or polling.
// Caller supplies a nonblocking timestamp; this API NEVER queries a clock.
// A failed FULL emission consumes an attempt sequence; BUSY is counted only
// by busy_drops because ordering a nested caller is not meaningful.
obs_recorder_result_t obs_recorder_emit(obs_recorder_t *recorder, unsigned lane,
                                         const obs_trace_record_t *event);

// Drain from ONE host/idle/worker consumer. Fair round-robin across lanes;
// no global timestamp ordering is inferred.
bool obs_recorder_pop(obs_recorder_t *recorder, obs_recorder_record_t *output);

// Concurrent counters are snapshots, NOT atomic/coherent across fields.
// Loss must be exported by a later transport; it must never be silently hidden.
bool obs_recorder_stats(const obs_recorder_t *recorder, unsigned lane,
                        obs_recorder_stats_t *output);

#ifdef __cplusplus
}
#endif
#endif
