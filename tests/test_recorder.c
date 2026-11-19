// SPDX-License-Identifier: Apache-2.0
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "obs_recorder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    obs_recorder_t recorder;
    obs_recorder_record_t got;
    obs_recorder_stats_t st;
    obs_trace_record_t data = {0u,42u,77u,3u,0u};

    assert(!obs_recorder_init(NULL,2u));
    assert(!obs_recorder_init(&recorder,0u));
    assert(!obs_recorder_init(&recorder,OBS_RECORDER_MAX_LANES+1u));
    assert(obs_recorder_init(&recorder,2u));
    assert(!obs_recorder_pop(NULL,&got));
    assert(!obs_recorder_pop(&recorder,NULL));
    assert(!obs_recorder_pop(&recorder,&got));
    assert(obs_recorder_emit(NULL,0,&data)==OBS_RECORDER_INVALID);
    assert(obs_recorder_emit(&recorder,0,NULL)==OBS_RECORDER_INVALID);
    assert(obs_recorder_emit(&recorder,2,&data)==OBS_RECORDER_INVALID);
    assert(!obs_recorder_stats(&recorder,2,&st));
    assert(!obs_recorder_stats(&recorder,0,NULL));
    assert(!obs_recorder_stats(NULL,0,&st));

    for(unsigned n=1u;n<=OBS_RECORDER_USABLE;++n) {
        data.timestamp=n;
        assert(obs_recorder_emit(&recorder,0,&data)==OBS_RECORDER_OK);
    }
    data.timestamp=1000u;
    assert(obs_recorder_emit(&recorder,0,&data)==OBS_RECORDER_FULL);
    assert(obs_recorder_stats(&recorder,0,&st));
    assert(st.attempted==OBS_RECORDER_USABLE+1u);
    assert(st.accepted==OBS_RECORDER_USABLE);
    assert(st.full_drops==1u && st.busy_drops==0u);
    assert(st.pending==OBS_RECORDER_USABLE);

    // Simulate an interrupt entering the same lane while a writer is active.
    assert(!atomic_flag_test_and_set_explicit(
        &recorder.lanes[0].writer_active,memory_order_acquire));
    assert(obs_recorder_emit(&recorder,0,&data)==OBS_RECORDER_BUSY);
    atomic_flag_clear_explicit(&recorder.lanes[0].writer_active,
                               memory_order_release);
    assert(obs_recorder_stats(&recorder,0,&st));
    assert(st.busy_drops==1u && st.attempted==OBS_RECORDER_USABLE+1u);

    // Separate active producer lane must remain independent despite lane 0 full.
    data.timestamp=9000u;
    data.core_id=1u;
    assert(obs_recorder_emit(&recorder,1,&data)==OBS_RECORDER_OK);
    assert(obs_recorder_pop(&recorder,&got) &&
           got.lane==0u && got.sequence==1u && got.event.timestamp==1u);
    assert(obs_recorder_pop(&recorder,&got) &&
           got.lane==1u && got.sequence==1u && got.event.timestamp==9000u &&
           got.schema_version==OBS_RECORDER_SCHEMA_VERSION);

    // Free slot and produce after overflow: sequence 64 was dropped.
    data.timestamp=7777u;
    data.core_id=0u;
    assert(obs_recorder_emit(&recorder,0,&data)==OBS_RECORDER_OK);
    unsigned observed=0u;
    while(obs_recorder_pop(&recorder,&got)) {
        assert(got.lane==0u);
        assert(got.event.task_id==42u && got.event.object_id==77u);
        assert(got.event.event_type==3u);
        if(got.sequence==OBS_RECORDER_USABLE+2u) {
            assert(got.event.timestamp==7777u);
            assert(observed==OBS_RECORDER_USABLE-1u);
        } else {
            assert(got.sequence==observed+2u);
            assert(got.event.timestamp==got.sequence);
        }
        ++observed;
    }
    assert(observed==OBS_RECORDER_USABLE);
    assert(obs_recorder_stats(&recorder,0,&st));
    assert(st.attempted==OBS_RECORDER_USABLE+2u);
    assert(st.accepted==OBS_RECORDER_USABLE+1u);
    assert(st.full_drops==1u && st.busy_drops==1u && st.pending==0u);
    assert(obs_recorder_stats(&recorder,1,&st));
    assert(st.attempted==1u && st.accepted==1u &&
           st.full_drops==0u && st.busy_drops==0u && st.pending==0u);

    // Explicit failure if initialized before use but later deactivated.
    recorder.ready=false;
    assert(obs_recorder_emit(&recorder,0,&data)==OBS_RECORDER_INVALID);
    assert(!obs_recorder_pop(&recorder,&got));
    assert(!obs_recorder_stats(&recorder,0,&st));
    puts("W16 deterministic multi-lane, FIFO, overflow/gaps and reentry telemetry OK");
    return 0;
}
