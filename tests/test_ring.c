// SPDX-License-Identifier: Apache-2.0
#include "obs_trace_ring.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
int main(void) {
    obs_trace_ring_t ring;
    obs_trace_ring_init(&ring);
    obs_trace_record_t sample = {123u, 42u, 55u, 9u, 0u};
    obs_trace_record_t result;
    assert(!obs_trace_ring_pop(&ring, &result));
    for (unsigned i=0;i<OBS_TRACE_RING_USABLE;i++) {
        sample.timestamp = i;
        assert(obs_trace_ring_push(&ring,&sample));
    }
    assert(!obs_trace_ring_push(&ring,&sample));
    assert(obs_trace_ring_dropped(&ring)==1u);
    for (unsigned i=0;i<OBS_TRACE_RING_USABLE;i++) {
        assert(obs_trace_ring_pop(&ring,&result));
        assert(result.timestamp == i);
    }
    assert(!obs_trace_ring_pop(&ring,&result));
    assert(obs_trace_ring_push(&ring,&sample));
    assert(obs_trace_ring_pop(&ring,&result));
    puts("SPSC trace ring tests passed");
    return 0;
}
