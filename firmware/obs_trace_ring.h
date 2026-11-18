// SPDX-License-Identifier: Apache-2.0
#ifndef OBS_TRACE_RING_H
#define OBS_TRACE_RING_H
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
// A value of 1 requires checking the actual atomic objects at initialization.
#if ATOMIC_INT_LOCK_FREE == 0
#error "This prototype requires lock-free atomic unsigned int; check target support."
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Deliberately bounded SPSC ring. One producer and one consumer only.
// Never call obs_trace_ring_push from multiple tasks/ISRs concurrently.
#ifndef OBS_TRACE_RING_CAPACITY
#define OBS_TRACE_RING_CAPACITY 128u
#endif
#define OBS_TRACE_RING_USABLE (OBS_TRACE_RING_CAPACITY - 1u)
typedef struct {
    uint64_t timestamp;
    uint32_t task_id;
    uint32_t object_id;
    uint16_t event_type;
    uint16_t core_id;
} obs_trace_record_t;
typedef struct {
    bool ready; // Set once before publishing the ring to producer/consumer.
    atomic_uint head;
    atomic_uint tail;
    atomic_uint dropped;
    obs_trace_record_t records[OBS_TRACE_RING_CAPACITY];
} obs_trace_ring_t;
// Returns false on unsupported atomics. Push/pop reject an unready ring.
// Initialize before concurrent use; never reinitialize an active ring.
bool obs_trace_ring_init(obs_trace_ring_t *ring);
bool obs_trace_ring_push(obs_trace_ring_t *ring, const obs_trace_record_t *record);
bool obs_trace_ring_pop(obs_trace_ring_t *ring, obs_trace_record_t *record);
unsigned obs_trace_ring_dropped(const obs_trace_ring_t *ring);
#ifdef __cplusplus
}
#endif
#endif
