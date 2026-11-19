// SPDX-License-Identifier: Apache-2.0
#ifndef OBS_WIRE_V1_H
#define OBS_WIRE_V1_H
#include "obs_trace_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Explicit endian-independent wire format; NEVER memcpy a native struct to the wire.
// 4 magic ("RTOB"), 1 version, 1 kind, 2 payload length (LE),
// 4 epoch (LE), 4 frame sequence (LE), payload, 4 CRC32/IEEE (LE).
// CRC32 detects accidental corruption, NOT malicious modification/authentication.
#define OBS_WIRE_V1 1u
#define OBS_WIRE_HEADER_BYTES 16u
#define OBS_WIRE_CRC_BYTES 4u
#define OBS_WIRE_EVENT_BYTES 28u
#define OBS_WIRE_STATS_BYTES 24u
#define OBS_WIRE_MAX_FRAME 48u
#define OBS_WIRE_MAX_LANES 32u
#define OBS_WIRE_EVENT 1u
#define OBS_WIRE_STATS 2u

typedef struct {
    uint16_t lane;
    obs_recorder_stats_t counters; // independently sampled, NOT coherent
} obs_wire_stats_t;

typedef struct {
    uint32_t epoch;            // application-selected nonzero connection epoch
    uint32_t sequence;         // transport per-epoch sequence, 1..UINT32_MAX
    uint8_t kind;
    bool new_epoch;            // including first observed epoch
    bool gap_before;           // one or more transport frames unobserved
    uint32_t missing_before;   // conservative per-epoch transport gap
    obs_recorder_record_t event;
    obs_wire_stats_t stats;
} obs_wire_frame_t;

typedef struct {
    uint8_t buffered[OBS_WIRE_MAX_FRAME];
    uint8_t used;
    bool have_epoch;
    uint32_t epoch, last_sequence;
    bool have_lane[OBS_WIRE_MAX_LANES];
    uint32_t last_lane_sequence[OBS_WIRE_MAX_LANES];
    uint32_t decoded;
    uint32_t crc_errors;
    uint32_t invalid_frames;
    uint32_t replayed_frames;
    uint32_t missing_frames;  // transport gaps; saturates
    uint32_t missing_attempts;// W16 lane sequence gaps; may overlap missing_frames
    uint32_t epoch_changes;
    bool loss_saturated;
} obs_wire_decoder_t;

typedef enum {
    OBS_WIRE_WAIT = 0,
    OBS_WIRE_FRAME_READY = 1,
    OBS_WIRE_REJECTED = 2
} obs_wire_result_t;

// pack returns exact frame size or 0 on malformed IDs, widths, zero epoch/seq.
// No memory allocation; output must hold OBS_WIRE_MAX_FRAME bytes.
size_t obs_wire_pack_event(uint8_t output[OBS_WIRE_MAX_FRAME],
                           uint32_t epoch, uint32_t frame_sequence,
                           const obs_recorder_record_t *record);
size_t obs_wire_pack_stats(uint8_t output[OBS_WIRE_MAX_FRAME],
                           uint32_t epoch, uint32_t frame_sequence,
                           const obs_wire_stats_t *stats);

void obs_wire_decoder_init(obs_wire_decoder_t *decoder);
// Push one byte at a time. On FRAME_READY output is valid. On REJECTED,
// resynchronisation retains possible "RTOB" prefixes; do not treat as session.
// 1 decoder = 1 unidirectional publisher. No concurrent feed allowed.
obs_wire_result_t obs_wire_decoder_push(obs_wire_decoder_t *decoder,
                                       uint8_t byte, obs_wire_frame_t *output);

#ifdef __cplusplus
}
#endif
#endif
