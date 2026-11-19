// SPDX-License-Identifier: Apache-2.0
#include "obs_recorder.h"
#include <limits.h>
#include <stddef.h>

_Static_assert(OBS_RECORDER_MAX_LANES >= 1u && OBS_RECORDER_MAX_LANES <= 32u,
               "W16 requires 1..32 statically allocated producer lanes");
_Static_assert(OBS_RECORDER_CAPACITY >= 2u &&
               (OBS_RECORDER_CAPACITY & (OBS_RECORDER_CAPACITY - 1u)) == 0u,
               "W16 ring capacity must be a power of 2, >=2");
_Static_assert(OBS_RECORDER_CAPACITY <= UINT_MAX / 2u,
               "W16 ring indices must fit unsigned int");
_Static_assert(OBS_RECORDER_MAX_LANES <= UINT16_MAX,
               "W16 lane id must fit wire record");
_Static_assert(sizeof(unsigned) >= sizeof(uint32_t),
               "W16 requires at least 32-bit unsigned atomics");

bool obs_recorder_init(obs_recorder_t *recorder, unsigned lane_count) {
    if (recorder == NULL || lane_count == 0u ||
        lane_count > OBS_RECORDER_MAX_LANES) return false;
    recorder->ready = false;
    recorder->lane_count = lane_count;
    recorder->consumer_cursor = 0u;
    for (unsigned i = 0u; i < lane_count; ++i) {
        obs_recorder_lane_t *lane = &recorder->lanes[i];
        atomic_init(&lane->head, 0u);
        atomic_init(&lane->tail, 0u);
        atomic_init(&lane->attempts, 0u);
        atomic_init(&lane->accepted, 0u);
        atomic_init(&lane->full_drops, 0u);
        atomic_init(&lane->busy_drops, 0u);
        atomic_flag_clear_explicit(&lane->writer_active, memory_order_relaxed);
        if (!atomic_is_lock_free(&lane->head) ||
            !atomic_is_lock_free(&lane->tail) ||
            !atomic_is_lock_free(&lane->attempts) ||
            !atomic_is_lock_free(&lane->accepted) ||
            !atomic_is_lock_free(&lane->full_drops) ||
            !atomic_is_lock_free(&lane->busy_drops))
            return false;
    }
    recorder->ready = true;
    return true;
}

obs_recorder_result_t obs_recorder_emit(obs_recorder_t *recorder, unsigned index,
                                        const obs_trace_record_t *event) {
    if (recorder == NULL || event == NULL ||
        !recorder->ready || index >= recorder->lane_count)
        return OBS_RECORDER_INVALID;

    obs_recorder_lane_t *lane = &recorder->lanes[index];
    if (atomic_flag_test_and_set_explicit(&lane->writer_active,
                                           memory_order_acquire)) {
        (void)atomic_fetch_add_explicit(&lane->busy_drops, 1u,
                                        memory_order_relaxed);
        return OBS_RECORDER_BUSY;
    }

    // Only the non-reentrant writer can modify this ring's payload.
    // Attempts increase even when full; the next accepted record exposes
    // the skipped sequence number. BUSY has separate loss telemetry.
    const unsigned sequence =
        atomic_fetch_add_explicit(&lane->attempts, 1u, memory_order_relaxed) + 1u;
    const unsigned head = atomic_load_explicit(&lane->head, memory_order_relaxed);
    const unsigned tail = atomic_load_explicit(&lane->tail, memory_order_acquire);
    const unsigned next = (head + 1u) & (OBS_RECORDER_CAPACITY - 1u);
    if (next == tail) {
        (void)atomic_fetch_add_explicit(&lane->full_drops, 1u,
                                        memory_order_relaxed);
        atomic_flag_clear_explicit(&lane->writer_active, memory_order_release);
        return OBS_RECORDER_FULL;
    }

    obs_recorder_record_t record;
    record.event = *event;
    record.sequence = (uint32_t)sequence;
    record.lane = (uint16_t)index;
    record.schema_version = OBS_RECORDER_SCHEMA_VERSION;
    lane->buffer[head] = record;
    atomic_store_explicit(&lane->head, next, memory_order_release);
    (void)atomic_fetch_add_explicit(&lane->accepted, 1u, memory_order_relaxed);
    atomic_flag_clear_explicit(&lane->writer_active, memory_order_release);
    return OBS_RECORDER_OK;
}

bool obs_recorder_pop(obs_recorder_t *recorder, obs_recorder_record_t *output) {
    if (recorder == NULL || output == NULL || !recorder->ready) return false;
    // Consumer cursor and tails are exclusively owned by one drain context.
    const unsigned lanes = recorder->lane_count;
    for (unsigned offset = 0u; offset < lanes; ++offset) {
        const unsigned i = (recorder->consumer_cursor + offset) % lanes;
        obs_recorder_lane_t *lane = &recorder->lanes[i];
        const unsigned tail = atomic_load_explicit(&lane->tail, memory_order_relaxed);
        const unsigned head = atomic_load_explicit(&lane->head, memory_order_acquire);
        if (tail == head) continue;
        *output = lane->buffer[tail];
        atomic_store_explicit(&lane->tail,
                              (tail + 1u) & (OBS_RECORDER_CAPACITY - 1u),
                              memory_order_release);
        recorder->consumer_cursor = (i + 1u) % lanes;
        return true;
    }
    return false;
}

bool obs_recorder_stats(const obs_recorder_t *recorder, unsigned index,
                         obs_recorder_stats_t *output) {
    if (recorder == NULL || output == NULL || !recorder->ready ||
        index >= recorder->lane_count) return false;
    const obs_recorder_lane_t *lane = &recorder->lanes[index];
    output->attempted = atomic_load_explicit(&lane->attempts, memory_order_relaxed);
    output->accepted = atomic_load_explicit(&lane->accepted, memory_order_relaxed);
    output->full_drops = atomic_load_explicit(&lane->full_drops, memory_order_relaxed);
    output->busy_drops = atomic_load_explicit(&lane->busy_drops, memory_order_relaxed);
    const unsigned head = atomic_load_explicit(&lane->head, memory_order_acquire);
    const unsigned tail = atomic_load_explicit(&lane->tail, memory_order_acquire);
    output->pending = (head - tail) & (OBS_RECORDER_CAPACITY - 1u);
    return true;
}
