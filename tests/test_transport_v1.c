// SPDX-License-Identifier: Apache-2.0
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "obs_recorder.h"
#include "obs_transport_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

typedef struct {
    uint8_t accepted[8][OBS_WIRE_MAX_FRAME];
    size_t size[8];
    unsigned count;
    unsigned retries;
    unsigned failures;
} mock_t;
static obs_port_status_t sink(void *context,const uint8_t *data,size_t size) {
    mock_t *mock=(mock_t *)context;
    assert(mock!=NULL && data!=NULL && size<=OBS_WIRE_MAX_FRAME);
    if(mock->retries) {
        --mock->retries;
        return OBS_PORT_RETRY;
    }
    if(mock->failures) {
        --mock->failures;
        return OBS_PORT_FAILED;
    }
    assert(mock->count<8u);
    memcpy(mock->accepted[mock->count],data,size);
    mock->size[mock->count++]=size;
    return OBS_PORT_ACCEPTED;
}
int main(void) {
    obs_transport_tx_t tx={0};
    obs_recorder_t recorder;
    obs_trace_record_t event={0};
    obs_wire_decoder_t decoder;
    obs_wire_frame_t out={0};
    mock_t mock={0};

    assert(!obs_transport_init(NULL,OBS_PORT_UART,9u,true,false));
    assert(!obs_transport_init(&tx,OBS_PORT_UART,0u,true,false));
    assert(!obs_transport_init(&tx,OBS_PORT_UART,9u,false,false));
    assert(!obs_transport_init(&tx,OBS_PORT_RTT,9u,true,false));
    assert(!obs_transport_init(&tx,(obs_port_kind_t)99,9u,true,true));
    assert(obs_transport_init(&tx,OBS_PORT_UART,9u,true,false));
    assert(obs_recorder_init(&recorder,1u));
    assert(obs_transport_pump(NULL,&recorder,sink,&mock)==OBS_TX_INVALID);
    assert(obs_transport_pump(&tx,NULL,sink,&mock)==OBS_TX_INVALID);
    assert(obs_transport_pump(&tx,&recorder,NULL,&mock)==OBS_TX_INVALID);
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_IDLE);

    for(unsigned i=1u;i<=3u;++i) {
        event.timestamp=i*10u;
        event.task_id=77u;
        event.core_id=0u;
        assert(obs_recorder_emit(&recorder,0u,&event)==OBS_RECORDER_OK);
    }
    mock.retries=1u;
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_RETRY);
    assert(tx.pending && tx.pending_size==48u);
    assert(tx.next_sequence==2u && tx.backpressure==1u && mock.count==0u);
    // Pending remains unchanged; another payload may not overwrite it.
    uint8_t hold[OBS_WIRE_MAX_FRAME];
    memcpy(hold,tx.pending_frame,tx.pending_size);
    assert(obs_transport_emit_stats(&tx,&recorder,0u,sink,&mock)==OBS_TX_RETRY);
    assert(tx.pending && memcmp(hold,tx.pending_frame,tx.pending_size)==0);
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_SENT);
    assert(tx.submitted==1u && !tx.pending && mock.count==1u);
    mock.failures=1u;
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_FAILED);
    assert(tx.failed==1u && !tx.pending && tx.next_sequence==3u);
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_SENT);
    assert(tx.submitted==2u && tx.next_sequence==4u);
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_IDLE);
    assert(obs_transport_emit_stats(&tx,&recorder,0u,sink,&mock)==OBS_TX_SENT);
    assert(tx.submitted==3u && mock.count==3u);
    assert(obs_transport_emit_stats(&tx,&recorder,9u,sink,&mock)==OBS_TX_INVALID);

    obs_wire_decoder_init(&decoder);
    for(unsigned f=0u;f<mock.count;++f) {
        obs_wire_result_t result=OBS_WIRE_WAIT;
        for(size_t i=0;i<mock.size[f];++i) {
            const obs_wire_result_t step=
                obs_wire_decoder_push(&decoder,mock.accepted[f][i],&out);
            if(step==OBS_WIRE_FRAME_READY)result=step;
        }
        assert(result==OBS_WIRE_FRAME_READY);
        if(f==0u) {
            assert(out.sequence==1u && out.event.sequence==1u);
            assert(!out.gap_before && out.event.event.timestamp==10u);
        } else if(f==1u) {
            assert(out.sequence==3u && out.event.sequence==3u);
            assert(out.gap_before && out.missing_before==1u);
            assert(out.event.event.timestamp==30u);
        } else {
            assert(out.sequence==4u && out.kind==OBS_WIRE_STATS);
            assert(out.stats.counters.attempted==3u);
            assert(out.stats.counters.accepted==3u);
        }
    }
    assert(decoder.missing_frames==1u && decoder.missing_attempts==1u);
    assert(decoder.crc_errors==0u && decoder.replayed_frames==0u);

    // Transport kind requires explicit endpoint ownership and RTT approval.
    assert(obs_transport_init(&tx,OBS_PORT_USB_CDC,10u,true,false));
    assert(obs_transport_init(&tx,OBS_PORT_TCP_OWNED,11u,true,false));
    assert(obs_transport_init(&tx,OBS_PORT_RTT,12u,true,true));
    // Sequence exhaustion must NOT silently wrap and reuse identifiers.
    assert(obs_transport_init(&tx,OBS_PORT_UART,13u,true,false));
    event.timestamp=40u;
    assert(obs_recorder_emit(&recorder,0u,&event)==OBS_RECORDER_OK);
    assert(obs_recorder_emit(&recorder,0u,&event)==OBS_RECORDER_OK);
    tx.next_sequence=UINT32_MAX;
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_SENT);
    assert(tx.next_sequence==0u);
    assert(obs_transport_pump(&tx,&recorder,sink,&mock)==OBS_TX_EPOCH_EXHAUSTED);
    // The second unread recorder event remains queued for explicit new epoch.
    obs_recorder_stats_t st;
    assert(obs_recorder_stats(&recorder,0u,&st) && st.pending==1u);
    puts("W17 bounded transport staging, backpressure, explicit drop, RTT gate and epoch exhaustion OK");
    return 0;
}
