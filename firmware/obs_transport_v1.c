// SPDX-License-Identifier: Apache-2.0
#include "obs_transport_v1.h"
#include "obs_recorder.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static void saturate(uint32_t *value) {
    if(*value!=UINT32_MAX)++*value;
}
bool obs_transport_init(obs_transport_tx_t *tx,obs_port_kind_t kind,
                        uint32_t epoch,bool endpoint_owned,
                        bool rtt_qualified) {
    if(tx==NULL || epoch==0u || !endpoint_owned ||
       (kind!=OBS_PORT_UART && kind!=OBS_PORT_USB_CDC &&
        kind!=OBS_PORT_TCP_OWNED && kind!=OBS_PORT_RTT) ||
       (kind==OBS_PORT_RTT && !rtt_qualified))return false;
    memset(tx,0,sizeof *tx);
    tx->ready=true;
    tx->kind=kind;
    tx->epoch=epoch;
    tx->next_sequence=1u;
    return true;
}
static obs_tx_result_t submit_pending(obs_transport_tx_t *tx,
                                      obs_port_submit_t submit,void *context) {
    if(!tx->pending)return OBS_TX_IDLE;
    const obs_port_status_t status=submit(context,tx->pending_frame,
                                         tx->pending_size);
    if(status==OBS_PORT_RETRY) {
        saturate(&tx->backpressure);
        return OBS_TX_RETRY;
    }
    tx->pending=false;
    tx->pending_size=0u;
    if(status==OBS_PORT_ACCEPTED) {
        saturate(&tx->submitted);
        return OBS_TX_SENT;
    }
    saturate(&tx->failed);
    return OBS_TX_FAILED;
}
static bool sequence_available(const obs_transport_tx_t *tx) {
    return tx->next_sequence!=0u; // exhausting UINT32_MAX requires new epoch
}
obs_tx_result_t obs_transport_pump(obs_transport_tx_t *tx,
                                   obs_recorder_t *recorder,
                                   obs_port_submit_t submit,void *context) {
    if(tx==NULL || !tx->ready || recorder==NULL || submit==NULL)
        return OBS_TX_INVALID;
    if(tx->pending)return submit_pending(tx,submit,context);
    if(!sequence_available(tx))return OBS_TX_EPOCH_EXHAUSTED;
    obs_recorder_record_t event;
    if(!obs_recorder_pop(recorder,&event))return OBS_TX_IDLE;
    const size_t size=obs_wire_pack_event(tx->pending_frame,tx->epoch,
                                          tx->next_sequence,&event);
    // Consumed event cannot be returned to W16. A malformed producer
    // record is counted as failed rather than misencoded/transmitted.
    if(!size) {
        saturate(&tx->failed);
        return OBS_TX_FAILED;
    }
    ++tx->next_sequence; // uint32_t 0 marks exhausted, never reused in epoch
    tx->pending_size=(uint8_t)size;
    tx->pending=true;
    return submit_pending(tx,submit,context);
}
obs_tx_result_t obs_transport_emit_stats(obs_transport_tx_t *tx,
                                         const obs_recorder_t *recorder,
                                         unsigned lane,obs_port_submit_t submit,
                                         void *context) {
    if(tx==NULL || !tx->ready || recorder==NULL || submit==NULL)
        return OBS_TX_INVALID;
    if(tx->pending)return OBS_TX_RETRY; // caller must pump pending before stats
    if(!sequence_available(tx))return OBS_TX_EPOCH_EXHAUSTED;
    obs_wire_stats_t stats={0};
    if(lane>=OBS_WIRE_MAX_LANES ||
       !obs_recorder_stats(recorder,lane,&stats.counters))
        return OBS_TX_INVALID;
    stats.lane=(uint16_t)lane;
    const size_t size=obs_wire_pack_stats(tx->pending_frame,tx->epoch,
                                          tx->next_sequence,&stats);
    if(!size)return OBS_TX_INVALID;
    ++tx->next_sequence;
    tx->pending_size=(uint8_t)size;
    tx->pending=true;
    return submit_pending(tx,submit,context);
}
