// SPDX-License-Identifier: Apache-2.0
#include "obs_trace_ring.h"
#include <stddef.h>
_Static_assert(OBS_TRACE_RING_CAPACITY > 1u &&
               (OBS_TRACE_RING_CAPACITY & (OBS_TRACE_RING_CAPACITY - 1u)) == 0u,
               "ring capacity must be a power of two");
bool obs_trace_ring_init(obs_trace_ring_t *ring) {
    if (ring == NULL) return false;
    ring->ready = false;
    atomic_init(&ring->head, 0u);
    atomic_init(&ring->tail, 0u);
    atomic_init(&ring->dropped, 0u);
    ring->ready = atomic_is_lock_free(&ring->head) &&
                  atomic_is_lock_free(&ring->tail) &&
                  atomic_is_lock_free(&ring->dropped);
    return ring->ready;
}
bool obs_trace_ring_push(obs_trace_ring_t *ring, const obs_trace_record_t *record) {
    if (ring == NULL || record == NULL || !ring->ready) return false;
    const unsigned head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    const unsigned tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    const unsigned next = (head + 1u) & (OBS_TRACE_RING_CAPACITY - 1u);
    if (next == tail) {
        atomic_fetch_add_explicit(&ring->dropped, 1u, memory_order_relaxed);
        return false;
    }
    ring->records[head] = *record;
    atomic_store_explicit(&ring->head, next, memory_order_release);
    return true;
}
bool obs_trace_ring_pop(obs_trace_ring_t *ring, obs_trace_record_t *record) {
    if (ring == NULL || record == NULL || !ring->ready) return false;
    const unsigned tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    const unsigned head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if (tail == head) return false;
    *record = ring->records[tail];
    atomic_store_explicit(&ring->tail, (tail + 1u) & (OBS_TRACE_RING_CAPACITY - 1u), memory_order_release);
    return true;
}
unsigned obs_trace_ring_dropped(const obs_trace_ring_t *ring) {
    return ring == NULL || !ring->ready ? 0u : atomic_load_explicit(&ring->dropped, memory_order_relaxed);
}
