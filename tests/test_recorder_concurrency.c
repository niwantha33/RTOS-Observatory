// SPDX-License-Identifier: Apache-2.0
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "obs_recorder.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdatomic.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#define EVENTS_PER_LANE 15000u
#define TEST_LANES 2u

typedef struct {
    obs_recorder_t *recorder;
    unsigned lane;
    atomic_bool done;
} producer_ctx_t;

static void run_producer(producer_ctx_t *context) {
    for (unsigned i=1u; i<=EVENTS_PER_LANE; ++i) {
        obs_trace_record_t event = {
            (uint64_t)i, 100u + context->lane,
            500u, 9u, (uint16_t)context->lane
        };
        const obs_recorder_result_t result =
            obs_recorder_emit(context->recorder,context->lane,&event);
        assert(result==OBS_RECORDER_OK || result==OBS_RECORDER_FULL);
    }
    atomic_store_explicit(&context->done,true,memory_order_release);
}
#if defined(_WIN32)
static DWORD WINAPI producer_thread(LPVOID parameter) {
    run_producer((producer_ctx_t *)parameter);
    return 0;
}
#else
static void *producer_thread(void *parameter) {
    run_producer((producer_ctx_t *)parameter);
    return NULL;
}
#endif

int main(void) {
    obs_recorder_t recorder;
    assert(obs_recorder_init(&recorder,TEST_LANES));
    producer_ctx_t producers[TEST_LANES];
#if defined(_WIN32)
    HANDLE threads[TEST_LANES];
#else
    pthread_t threads[TEST_LANES];
#endif
    for(unsigned i=0u;i<TEST_LANES;++i) {
        producers[i].recorder=&recorder;
        producers[i].lane=i;
        atomic_init(&producers[i].done,false);
#if defined(_WIN32)
        threads[i]=CreateThread(NULL,0,producer_thread,&producers[i],0,NULL);
        assert(threads[i]!=NULL);
#else
        assert(pthread_create(&threads[i],NULL,producer_thread,&producers[i])==0);
#endif
    }

    unsigned last_sequence[TEST_LANES]={0u};
    unsigned popped[TEST_LANES]={0u};
    unsigned idle_spins=0u;
    for(;;) {
        obs_recorder_record_t record;
        if(obs_recorder_pop(&recorder,&record)) {
            assert(record.schema_version==OBS_RECORDER_SCHEMA_VERSION);
            assert(record.lane<TEST_LANES);
            const unsigned lane=record.lane;
            assert(record.sequence>last_sequence[lane]);
            assert(record.sequence<=EVENTS_PER_LANE);
            assert(record.event.timestamp==record.sequence);
            assert(record.event.task_id==100u+lane);
            assert(record.event.core_id==lane);
            last_sequence[lane]=record.sequence;
            ++popped[lane];
            idle_spins=0u;
            continue;
        }
        const bool done =
            atomic_load_explicit(&producers[0].done,memory_order_acquire) &&
            atomic_load_explicit(&producers[1].done,memory_order_acquire);
        if(done)break;
        assert(++idle_spins<100000000u);
#if defined(_WIN32)
        Sleep(0);
#else
        sched_yield();
#endif
    }

    for(unsigned i=0u;i<TEST_LANES;++i) {
#if defined(_WIN32)
        assert(WaitForSingleObject(threads[i],INFINITE)==WAIT_OBJECT_0);
        assert(CloseHandle(threads[i])!=0);
#else
        assert(pthread_join(threads[i],NULL)==0);
#endif
        obs_recorder_stats_t status;
        assert(obs_recorder_stats(&recorder,i,&status));
        assert(status.attempted==EVENTS_PER_LANE);
        assert(status.accepted==popped[i]);
        assert(status.full_drops==EVENTS_PER_LANE-popped[i]);
        assert(status.busy_drops==0u);
        assert(status.pending==0u);
        assert(status.accepted>0u);
    }
    puts("W16 two concurrent producers, single drain, loss accounting OK");
    return 0;
}
