#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only W17 frame audit for W19 physical evidence; no inferred tail loss."""
from __future__ import annotations

from pathlib import Path
import struct
import zlib


class WireAuditError(ValueError):
    pass


def audit_trace(path: Path) -> dict:
    content = path.read_bytes()
    if not content or len(content) > 8 * 1024 * 1024:
        raise WireAuditError("W19 trace empty/over size cap")
    at = 0
    prior_epoch = None
    last_frame = 0
    seen_epochs = set()
    lanes = {}
    stats_by_epoch_lane = {}
    events = frames = statistics = missing_frames = missing_attempts = 0
    while at < len(content):
        if len(content) - at < 20:
            raise WireAuditError("W19 raw trace truncated header/CRC")
        magic, version, kind, length, epoch, sequence = struct.unpack_from(
            "<4sBBHII", content, at)
        if magic != b"RTOB" or version != 1 or epoch == 0 or sequence == 0:
            raise WireAuditError("W19 raw trace invalid protocol/epoch/sequence")
        if (kind, length) not in ((1, 28), (2, 24)):
            raise WireAuditError("W19 raw trace unrecognized frame kind/size")
        frame_length = 16 + length + 4
        if at + frame_length > len(content):
            raise WireAuditError("W19 raw trace truncated payload")
        expected = struct.unpack_from("<I", content, at + frame_length-4)[0]
        observed = zlib.crc32(content[at:at + frame_length-4])
        if observed != expected:
            raise WireAuditError("W19 raw trace CRC mismatch")
        if epoch != prior_epoch:
            if epoch in seen_epochs:
                raise WireAuditError("W19 raw trace reused publisher epoch")
            seen_epochs.add(epoch)
            prior_epoch = epoch
            last_frame = 0
            lanes = {}
        if sequence <= last_frame:
            raise WireAuditError("W19 raw trace replayed/regressed frame")
        missing_frames += sequence-last_frame-1
        last_frame = sequence
        at += frame_length
        frames += 1
        payload = content[at-frame_length+16:at-4]
        if kind == 1:
            _tick, _task, _object, _event, _core, attempt, lane, schema = (
                struct.unpack("<QIIHHIHH", payload))
            if lane >= 32 or attempt == 0 or schema != 1:
                raise WireAuditError("W19 raw trace invalid recorder identity")
            previous = lanes.get(lane, 0)
            if attempt <= previous:
                raise WireAuditError("W19 raw trace regressed lane sequence")
            missing_attempts += attempt-previous-1
            lanes[lane] = attempt
            events += 1
        else:
            lane, reserved, attempted, accepted, full, busy, pending = (
                struct.unpack("<HHIIIII", payload))
            if lane >= 32 or reserved != 0:
                raise WireAuditError("W19 raw trace invalid stats payload")
            # Snapshots are not coherent; do not infer counts from each other.
            stats_by_epoch_lane[(epoch, lane)] = (attempted, accepted,
                                                   full, busy, pending)
            statistics += 1
    return {
        "bytes": len(content),
        "frames": frames,
        "events": events,
        "stats_frames": statistics,
        "publisher_epochs": len(seen_epochs),
        "missing_wire_frames": missing_frames,
        "missing_source_attempts": missing_attempts,
        "latest_declared_full_drops": sum(s[2] for s in stats_by_epoch_lane.values()),
        "latest_declared_busy_drops": sum(s[3] for s in stats_by_epoch_lane.values()),
        "stats_lanes": len(stats_by_epoch_lane),
        "tail_loss_unknown": True,
    }
