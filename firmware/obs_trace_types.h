// SPDX-License-Identifier: Apache-2.0
#ifndef OBS_TRACE_TYPES_H
#define OBS_TRACE_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// POD wire/analysis types shared with C++20 host code. Never contain
// C11 stdatomic objects; these are intentionally ABI-neutral field records.
#define OBS_RECORDER_SCHEMA_VERSION 1u
typedef struct {
    uint64_t timestamp;
    uint32_t task_id;
    uint32_t object_id;
    uint16_t event_type;
    uint16_t core_id;
} obs_trace_record_t;
typedef struct {
    obs_trace_record_t event;
    uint32_t sequence;
    uint16_t lane;
    uint16_t schema_version;
} obs_recorder_record_t;
typedef struct {
    unsigned attempted;
    unsigned accepted;
    unsigned full_drops;
    unsigned busy_drops;
    unsigned pending;
} obs_recorder_stats_t;

#ifdef __cplusplus
}
#endif
#endif
