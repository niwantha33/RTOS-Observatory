// SPDX-License-Identifier: Apache-2.0
#include "obs_wire_v1.h"
#include <limits.h>
#include <string.h>

_Static_assert(OBS_WIRE_HEADER_BYTES+OBS_WIRE_EVENT_BYTES+
               OBS_WIRE_CRC_BYTES==OBS_WIRE_MAX_FRAME, "wrong W17 max frame");
_Static_assert(OBS_WIRE_MAX_LANES==32u,"W17 must accept W16's 32-lane limit");
_Static_assert(sizeof(uint32_t)==4u && sizeof(uint64_t)==8u,
               "W17 requires exact-width integer types");

static const uint8_t magic[4]={'R','T','O','B'};

static void put16(uint8_t *p,uint16_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8u);
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(n>>(8u*i));
}
static void put64(uint8_t *p,uint64_t n) {
    for(unsigned i=0u;i<8u;++i)p[i]=(uint8_t)(n>>(8u*i));
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t n=0u;
    for(unsigned i=0u;i<4u;++i)n|=(uint32_t)p[i]<<(8u*i);
    return n;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t n=0u;
    for(unsigned i=0u;i<8u;++i)n|=(uint64_t)p[i]<<(8u*i);
    return n;
}
static uint32_t crc32_ieee(const uint8_t *p,size_t size) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0u;i<size;++i) {
        crc^=p[i];
        for(unsigned j=0u;j<8u;++j)
            crc=(crc>>1u)^(UINT32_C(0xedb88320)&
                            (0u-(crc&1u)));
    }
    return ~crc;
}
static size_t begin_frame(uint8_t *out,uint8_t kind,uint16_t bytes,
                          uint32_t epoch,uint32_t sequence) {
    if(out==NULL || epoch==0u || sequence==0u)return 0u;
    memcpy(out,magic,sizeof magic);
    out[4]=OBS_WIRE_V1;
    out[5]=kind;
    put16(out+6u,bytes);
    put32(out+8u,epoch);
    put32(out+12u,sequence);
    return OBS_WIRE_HEADER_BYTES+bytes+OBS_WIRE_CRC_BYTES;
}
size_t obs_wire_pack_event(uint8_t out[OBS_WIRE_MAX_FRAME],
                           uint32_t epoch,uint32_t frame_sequence,
                           const obs_recorder_record_t *record) {
    if(record==NULL || record->lane>=OBS_WIRE_MAX_LANES ||
       record->schema_version!=OBS_RECORDER_SCHEMA_VERSION ||
       record->sequence==0u)return 0u;
    const size_t size=begin_frame(out,OBS_WIRE_EVENT,
                                 OBS_WIRE_EVENT_BYTES,epoch,frame_sequence);
    if(!size)return 0u;
    uint8_t *p=out+OBS_WIRE_HEADER_BYTES;
    put64(p,record->event.timestamp);
    put32(p+8u,record->event.task_id);
    put32(p+12u,record->event.object_id);
    put16(p+16u,record->event.event_type);
    put16(p+18u,record->event.core_id);
    put32(p+20u,record->sequence);
    put16(p+24u,record->lane);
    put16(p+26u,record->schema_version);
    put32(out+size-4u,crc32_ieee(out,size-4u));
    return size;
}
size_t obs_wire_pack_stats(uint8_t out[OBS_WIRE_MAX_FRAME],
                           uint32_t epoch,uint32_t frame_sequence,
                           const obs_wire_stats_t *stats) {
    if(stats==NULL || stats->lane>=OBS_WIRE_MAX_LANES)return 0u;
    const size_t size=begin_frame(out,OBS_WIRE_STATS,
                                 OBS_WIRE_STATS_BYTES,epoch,frame_sequence);
    if(!size)return 0u;
    uint8_t *p=out+OBS_WIRE_HEADER_BYTES;
    put16(p,stats->lane);
    put16(p+2u,0u); // reserved; decoder refuses nonzero
    put32(p+4u,stats->counters.attempted);
    put32(p+8u,stats->counters.accepted);
    put32(p+12u,stats->counters.full_drops);
    put32(p+16u,stats->counters.busy_drops);
    put32(p+20u,stats->counters.pending);
    put32(out+size-4u,crc32_ieee(out,size-4u));
    return size;
}
void obs_wire_decoder_init(obs_wire_decoder_t *decoder) {
    if(decoder!=NULL)memset(decoder,0,sizeof *decoder);
}
static void add_loss(obs_wire_decoder_t *decoder,uint32_t *sum,
                     uint32_t missing) {
    if(UINT32_MAX-*sum<missing) {
        *sum=UINT32_MAX;
        decoder->loss_saturated=true;
    } else *sum+=missing;
}
static void retain_magic_prefix(obs_wire_decoder_t *decoder) {
    // Retain a suffix matching the start of magic after corruption/noise.
    const unsigned max=decoder->used<3u ? decoder->used : 3u;
    for(unsigned n=max;n>0u;--n) {
        if(memcmp(decoder->buffered+decoder->used-n,magic,n)==0) {
            memmove(decoder->buffered,decoder->buffered+decoder->used-n,n);
            decoder->used=(uint8_t)n;
            return;
        }
    }
    decoder->used=0u;
}
obs_wire_result_t obs_wire_decoder_push(obs_wire_decoder_t *decoder,
                                       uint8_t byte,obs_wire_frame_t *output) {
    if(decoder==NULL || output==NULL)return OBS_WIRE_REJECTED;
    if(decoder->used>=OBS_WIRE_MAX_FRAME) {
        decoder->invalid_frames++;
        decoder->used=0u;
    }
    decoder->buffered[decoder->used++]=byte;
    const uint8_t *p=decoder->buffered;
    if(decoder->used<=4u) {
        if(p[decoder->used-1u]!=magic[decoder->used-1u]) {
            retain_magic_prefix(decoder);
            return OBS_WIRE_REJECTED;
        }
        return OBS_WIRE_WAIT;
    }
    if(decoder->used==8u) {
        const uint16_t bytes=get16(p+6u);
        if(p[4]!=OBS_WIRE_V1 ||
           !((p[5]==OBS_WIRE_EVENT && bytes==OBS_WIRE_EVENT_BYTES) ||
             (p[5]==OBS_WIRE_STATS && bytes==OBS_WIRE_STATS_BYTES))) {
            ++decoder->invalid_frames;
            retain_magic_prefix(decoder);
            return OBS_WIRE_REJECTED;
        }
    }
    if(decoder->used<8u)return OBS_WIRE_WAIT;
    const size_t total=OBS_WIRE_HEADER_BYTES+get16(p+6u)+OBS_WIRE_CRC_BYTES;
    if(total>OBS_WIRE_MAX_FRAME) {
        ++decoder->invalid_frames;
        retain_magic_prefix(decoder);
        return OBS_WIRE_REJECTED;
    }
    if(decoder->used<total)return OBS_WIRE_WAIT;
    if(get32(p+total-4u)!=crc32_ieee(p,total-4u)) {
        ++decoder->crc_errors;
        retain_magic_prefix(decoder);
        return OBS_WIRE_REJECTED;
    }
    const uint32_t epoch=get32(p+8u);
    const uint32_t sequence=get32(p+12u);
    if(epoch==0u || sequence==0u) {
        ++decoder->invalid_frames;
        retain_magic_prefix(decoder);
        return OBS_WIRE_REJECTED;
    }
    obs_wire_frame_t found={0};
    found.epoch=epoch;
    found.sequence=sequence;
    found.kind=p[5];
    const uint8_t *data=p+OBS_WIRE_HEADER_BYTES;
    if(found.kind==OBS_WIRE_EVENT) {
        found.event.event.timestamp=get64(data);
        found.event.event.task_id=get32(data+8u);
        found.event.event.object_id=get32(data+12u);
        found.event.event.event_type=get16(data+16u);
        found.event.event.core_id=get16(data+18u);
        found.event.sequence=get32(data+20u);
        found.event.lane=get16(data+24u);
        found.event.schema_version=get16(data+26u);
        if(found.event.lane>=OBS_WIRE_MAX_LANES ||
           found.event.schema_version!=OBS_RECORDER_SCHEMA_VERSION ||
           found.event.sequence==0u) {
            ++decoder->invalid_frames;
            retain_magic_prefix(decoder);
            return OBS_WIRE_REJECTED;
        }
    } else {
        found.stats.lane=get16(data);
        found.stats.counters.attempted=get32(data+4u);
        found.stats.counters.accepted=get32(data+8u);
        found.stats.counters.full_drops=get32(data+12u);
        found.stats.counters.busy_drops=get32(data+16u);
        found.stats.counters.pending=get32(data+20u);
        if(found.stats.lane>=OBS_WIRE_MAX_LANES || get16(data+2u)!=0u) {
            ++decoder->invalid_frames;
            retain_magic_prefix(decoder);
            return OBS_WIRE_REJECTED;
        }
    }
    const bool new_epoch=!decoder->have_epoch || epoch!=decoder->epoch;
    if(!new_epoch) {
        if(sequence<=decoder->last_sequence ||
           (found.kind==OBS_WIRE_EVENT &&
            decoder->have_lane[found.event.lane] &&
            found.event.sequence<=decoder->last_lane_sequence[found.event.lane])) {
            ++decoder->replayed_frames;
            retain_magic_prefix(decoder);
            return OBS_WIRE_REJECTED;
        }
    } else {
        if(decoder->have_epoch)++decoder->epoch_changes;
        memset(decoder->have_lane,0,sizeof decoder->have_lane);
        memset(decoder->last_lane_sequence,0,sizeof decoder->last_lane_sequence);
        decoder->epoch=epoch;
        decoder->have_epoch=true;
    }
    found.new_epoch=new_epoch;
    const uint32_t previous=new_epoch ? 0u : decoder->last_sequence;
    found.missing_before=sequence-previous-1u;
    found.gap_before=found.missing_before!=0u;
    add_loss(decoder,&decoder->missing_frames,found.missing_before);
    decoder->last_sequence=sequence;
    if(found.kind==OBS_WIRE_EVENT) {
        const unsigned lane=found.event.lane;
        const uint32_t earlier=decoder->have_lane[lane] ?
                               decoder->last_lane_sequence[lane] : 0u;
        add_loss(decoder,&decoder->missing_attempts,
                 found.event.sequence-earlier-1u);
        decoder->have_lane[lane]=true;
        decoder->last_lane_sequence[lane]=found.event.sequence;
    }
    ++decoder->decoded;
    *output=found;
    decoder->used=0u;
    return OBS_WIRE_FRAME_READY;
}
