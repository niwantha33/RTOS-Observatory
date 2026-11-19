// SPDX-License-Identifier: Apache-2.0
#ifndef OBS_TRANSPORT_V1_H
#define OBS_TRANSPORT_V1_H
#include "obs_wire_v1.h"
// The atomic recorder is opaque to host-side C++ transport consumers.
typedef struct obs_recorder_t obs_recorder_t;

#ifdef __cplusplus
extern "C" {
#endif

// Transport-neutral, bounded transmit queue with ONE staged frame.
// Every actual UART, USB CDC, owned TCP socket or RTT binding is injected
// by the platform; NO probe/port/socket is opened by this library.
// Backends MUST atomically accept a whole frame for later delivery, or
// return RETRY without consuming bytes. Partial frame writes are forbidden.
typedef enum {
    OBS_PORT_UART = 1,
    OBS_PORT_USB_CDC = 2,
    OBS_PORT_TCP_OWNED = 3,
    OBS_PORT_RTT = 4
} obs_port_kind_t;
typedef enum {
    OBS_PORT_ACCEPTED = 0,
    OBS_PORT_RETRY = 1,
    OBS_PORT_FAILED = 2
} obs_port_status_t;
typedef obs_port_status_t (*obs_port_submit_t)(void *context,
                                                const uint8_t *data,
                                                size_t length);
typedef enum {
    OBS_TX_INVALID = 0,
    OBS_TX_IDLE = 1,
    OBS_TX_SENT = 2,    // sink accepted the *whole* frame, not host received
    OBS_TX_RETRY = 3,   // kept staged; no data overwritten
    OBS_TX_FAILED = 4,  // sender drops staged frame; loss accounted
    OBS_TX_EPOCH_EXHAUSTED = 5
} obs_tx_result_t;

typedef struct {
    uint8_t pending_frame[OBS_WIRE_MAX_FRAME];
    uint8_t pending_size;
    bool pending;
    bool ready;
    obs_port_kind_t kind;
    uint32_t epoch;
    uint32_t next_sequence; // 1..UINT32_MAX; zero means exhausted, renew epoch
    uint32_t submitted;
    uint32_t backpressure;
    uint32_t failed;
} obs_transport_tx_t;

// caller owns/configures endpoint and controls whether a route is safe.
// RTT must be explicitly qualified by the existing debugger owner; absence
// of a validated RTT implementation is NOT silently upgraded to supported.
// Invalid ports, epoch 0, and unqualified endpoints fail closed.
bool obs_transport_init(obs_transport_tx_t *tx,obs_port_kind_t kind,
                        uint32_t epoch,bool endpoint_owned,
                        bool rtt_qualified);

// One drain thread owns tx AND obs_recorder_pop; never ISR.
// Stage one recorder event and try to deliver via user-supplied all-or-nothing
// async sink. RETRY keeps bytes/stage without popping another event.
obs_tx_result_t obs_transport_pump(obs_transport_tx_t *tx,
                                   obs_recorder_t *recorder,
                                   obs_port_submit_t submit,void *context);

// Independently sampled W16 counters are transported explicitly.
// Refuses to enqueue if a frame is already staged. Caller retries later.
obs_tx_result_t obs_transport_emit_stats(obs_transport_tx_t *tx,
                                         const obs_recorder_t *recorder,
                                         unsigned lane,obs_port_submit_t submit,
                                         void *context);

// On failed frames, the next frame sequence reflects the loss. A sink must
// *not* return ACCEPTED until an entire frame has been queued atomically.
// A disconnect/reconnect needs a NEW epoch and a new tx object.
#ifdef __cplusplus
}
#endif
#endif
