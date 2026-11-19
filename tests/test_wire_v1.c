// SPDX-License-Identifier: Apache-2.0
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "obs_wire_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static obs_wire_result_t feed(obs_wire_decoder_t *decoder,
                              const uint8_t *bytes,size_t count,
                              obs_wire_frame_t *out) {
    obs_wire_result_t result=OBS_WIRE_WAIT;
    for(size_t i=0;i<count;++i) {
        const obs_wire_result_t step=obs_wire_decoder_push(decoder,bytes[i],out);
        if(step==OBS_WIRE_FRAME_READY)result=step;
    }
    return result;
}
static obs_recorder_record_t event(uint32_t sequence) {
    obs_recorder_record_t v={0};
    v.event.timestamp=UINT64_C(0x0102030405060708);
    v.event.task_id=UINT32_C(0x11223344);
    v.event.object_id=UINT32_C(0x55667788);
    v.event.event_type=UINT16_C(0x99aa);
    v.event.core_id=UINT16_C(0xbbcc);
    v.sequence=sequence;
    v.lane=2u;
    v.schema_version=OBS_RECORDER_SCHEMA_VERSION;
    return v;
}
int main(void) {
    uint8_t bytes[OBS_WIRE_MAX_FRAME]={0};
    const obs_recorder_record_t one=event(1u);
    const size_t size=obs_wire_pack_event(bytes,7u,1u,&one);
    assert(size==48u);
    const uint8_t golden[]={
        0x52,0x54,0x4f,0x42,0x01,0x01,0x1c,0x00,
        0x07,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
        0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,
        0x44,0x33,0x22,0x11,0x88,0x77,0x66,0x55,
        0xaa,0x99,0xcc,0xbb,0x01,0x00,0x00,0x00,
        0x02,0x00,0x01,0x00,0x4d,0x11,0xb5,0x00
    };
    assert(sizeof golden==size && memcmp(bytes,golden,size)==0);
    obs_wire_decoder_t decoder;
    obs_wire_frame_t output;
    obs_wire_decoder_init(&decoder);
    // Fragmented arbitrary input: no complete frame before last byte.
    for(size_t i=0;i<size-1u;++i)
        assert(obs_wire_decoder_push(&decoder,bytes[i],&output)==OBS_WIRE_WAIT);
    assert(obs_wire_decoder_push(&decoder,bytes[size-1u],&output)==OBS_WIRE_FRAME_READY);
    assert(output.kind==OBS_WIRE_EVENT && output.new_epoch && !output.gap_before);
    assert(output.epoch==7u && output.sequence==1u);
    assert(output.event.sequence==1u && output.event.lane==2u);
    assert(output.event.event.timestamp==one.event.timestamp);
    assert(output.event.event.task_id==one.event.task_id);
    assert(output.event.event.object_id==one.event.object_id);
    assert(output.event.event.event_type==one.event.event_type);
    assert(output.event.event.core_id==one.event.core_id);
    assert(decoder.missing_frames==0u && decoder.missing_attempts==0u);

    // A genuine duplicate frame must not be reported as new evidence.
    assert(feed(&decoder,bytes,size,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.replayed_frames==1u && decoder.decoded==1u);

    // Same epoch, missing wire sequence 2 and source attempt 2.
    obs_recorder_record_t third=event(3u);
    assert(obs_wire_pack_event(bytes,7u,3u,&third)==size);
    assert(feed(&decoder,bytes,size,&output)==OBS_WIRE_FRAME_READY);
    assert(output.gap_before && output.missing_before==1u && !output.new_epoch);
    assert(decoder.missing_frames==1u && decoder.missing_attempts==1u);

    // Stats are not assumed coherent, nor do they synthesize event timestamps.
    obs_wire_stats_t status={0};
    status.lane=2u;
    status.counters.attempted=4u;
    status.counters.accepted=3u;
    status.counters.full_drops=1u;
    status.counters.busy_drops=2u;
    status.counters.pending=1u;
    assert(obs_wire_pack_stats(bytes,7u,4u,&status)==44u);
    assert(feed(&decoder,bytes,44u,&output)==OBS_WIRE_FRAME_READY);
    assert(output.kind==OBS_WIRE_STATS && output.stats.lane==2u);
    assert(output.stats.counters.full_drops==1u);
    assert(output.stats.counters.busy_drops==2u);
    assert(!output.new_epoch && !output.gap_before);

    // A new explicit epoch is not continuously merged into previous clocks.
    assert(obs_wire_pack_event(bytes,8u,1u,&one)==size);
    assert(feed(&decoder,bytes,size,&output)==OBS_WIRE_FRAME_READY);
    assert(output.new_epoch && decoder.epoch_changes==1u &&
           decoder.epoch==8u && decoder.missing_frames==1u);

    // Garbage, invalid CRC, and a valid frame afterward: decoder resynchronizes.
    obs_wire_decoder_init(&decoder);
    assert(obs_wire_decoder_push(&decoder,'X',&output)==OBS_WIRE_REJECTED);
    assert(obs_wire_pack_event(bytes,7u,1u,&one)==size);
    bytes[25]^=0x20u;
    assert(feed(&decoder,bytes,size,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.crc_errors==1u);
    assert(obs_wire_pack_event(bytes,7u,2u,&one)==size);
    assert(feed(&decoder,bytes,size,&output)==OBS_WIRE_FRAME_READY);
    assert(output.new_epoch && output.gap_before && output.missing_before==1u);

    // Wrong lengths, unknown type or version: do not allocate or retain payload.
    obs_wire_decoder_init(&decoder);
    assert(obs_wire_pack_event(bytes,5u,1u,&one)==size);
    bytes[6]=0xffu;bytes[7]=0xffu;
    assert(feed(&decoder,bytes,8u,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.invalid_frames==1u);
    obs_wire_decoder_init(&decoder);
    assert(obs_wire_pack_event(bytes,5u,1u,&one)==size);
    bytes[4]=99u;
    assert(feed(&decoder,bytes,8u,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.invalid_frames==1u);
    obs_wire_decoder_init(&decoder);
    assert(obs_wire_pack_event(bytes,5u,1u,&one)==size);
    bytes[5]=99u;
    assert(feed(&decoder,bytes,8u,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.invalid_frames==1u);

    // No zero epoch/sequence and no out-of-range lane or legacy schema.
    obs_recorder_record_t wrong=one;
    assert(obs_wire_pack_event(bytes,0u,1u,&one)==0u);
    assert(obs_wire_pack_event(bytes,7u,0u,&one)==0u);
    wrong.lane=OBS_WIRE_MAX_LANES;
    assert(obs_wire_pack_event(bytes,7u,1u,&wrong)==0u);
    wrong=one;wrong.schema_version=0u;
    assert(obs_wire_pack_event(bytes,7u,1u,&wrong)==0u);
    wrong=one;wrong.sequence=0u;
    assert(obs_wire_pack_event(bytes,7u,1u,&wrong)==0u);
    status.lane=OBS_WIRE_MAX_LANES;
    assert(obs_wire_pack_stats(bytes,7u,1u,&status)==0u);
    assert(obs_wire_pack_stats(bytes,0u,1u,NULL)==0u);
    assert(obs_wire_decoder_push(NULL,0u,&output)==OBS_WIRE_REJECTED);
    assert(obs_wire_decoder_push(&decoder,0u,NULL)==OBS_WIRE_REJECTED);

    // Late attach and near-32-bit sequence exhaustion: counted, not wrapped.
    obs_wire_decoder_init(&decoder);
    assert(obs_wire_pack_event(bytes,123u,UINT32_MAX,&one)==size);
    assert(feed(&decoder,bytes,size,&output)==OBS_WIRE_FRAME_READY);
    assert(output.new_epoch && output.gap_before);
    assert(decoder.missing_frames==UINT32_MAX-1u);
    assert(decoder.loss_saturated==false);
    assert(obs_wire_pack_event(bytes,123u,1u,&one)==size);
    assert(feed(&decoder,bytes,size,&output)!=OBS_WIRE_FRAME_READY);
    assert(decoder.replayed_frames==1u);
    puts("W17 golden-byte CRC32/LE, fragmentation, corruption, gaps, epochs and replay OK");
    return 0;
}
