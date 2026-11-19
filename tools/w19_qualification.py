#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""W19 hardware qualification evidence audit. Never self-certifies hardware."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys

SCHEMA = "rtos-observatory-w19-v1"
MAX_JSON = 128 * 1024
MAX_ATTACHMENT = 8 * 1024 * 1024
MAX_SAMPLES = 1024
SUPPORTED = {"UART", "USB_CDC", "TCP_OWNED", "RTT"}
REQUIRED_ATTACHMENTS = {
    "firmware_map",
    "probe_log",
    "independent_timing",
    "raw_trace",
    "baseline_measurement",
}
HEX_SHA = re.compile(r"[0-9a-f]{64}\Z")
LABEL = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+:-]{0,127}\Z")


class EvidenceError(ValueError):
    pass


def check_object(obj, label, required, optional=()):
    if not isinstance(obj, dict):
        raise EvidenceError(f"{label}: expected object")
    required, allowed = set(required), set(required) | set(optional)
    absent, extra = required - obj.keys(), obj.keys() - allowed
    if absent or extra:
        raise EvidenceError(f"{label}: missing {sorted(absent)} / unexpected {sorted(extra)}")
    return obj


def identifier(value, label):
    if not isinstance(value, str) or not LABEL.fullmatch(value):
        raise EvidenceError(f"{label}: invalid bounded identifier")
    return value


def boolean(value, label):
    if type(value) is not bool:
        raise EvidenceError(f"{label}: expected boolean")
    return value


def integer(value, label, low=0, high=2**53-1):
    if type(value) is not int or not low <= value <= high:
        raise EvidenceError(f"{label}: invalid bounded integer")
    return value


def samples(value, label):
    if not isinstance(value, list) or not 3 <= len(value) <= MAX_SAMPLES:
        raise EvidenceError(f"{label}: require 3..{MAX_SAMPLES} independent samples")
    return [integer(x, label, 1, 2**32-1) for x in value]


def attachment(root, item):
    check_object(item, "attachment", {"kind", "path", "sha256"})
    kind = identifier(item["kind"], "attachment kind")
    rel = item["path"]
    if not isinstance(rel, str) or not rel or len(rel) > 240:
        raise EvidenceError("attachment: invalid local path")
    p = Path(rel)
    if p.is_absolute() or ".." in p.parts or any(part in ("", ".") for part in p.parts):
        raise EvidenceError("attachment: absolute/traversal path rejected")
    resolved = root / p
    for parent in (root, *(root / Path(*p.parts[:i]) for i in range(1, len(p.parts)+1))):
        if parent.is_symlink():
            raise EvidenceError("attachment: symlink rejected")
    if not resolved.is_file() or resolved.stat().st_size > MAX_ATTACHMENT:
        raise EvidenceError("attachment: file missing/oversized")
    expected = item["sha256"]
    if not isinstance(expected, str) or not HEX_SHA.fullmatch(expected):
        raise EvidenceError("attachment: invalid SHA256")
    digest = hashlib.sha256()
    with resolved.open("rb") as source:
        while chunk := source.read(65536):
            digest.update(chunk)
    if digest.hexdigest() != expected:
        raise EvidenceError("attachment: SHA256 mismatch")
    return kind


def summarize(profile: dict, root: Path):
    check_object(
        profile, "manifest",
        {"schema", "sample_origin", "board", "silicon", "probe",
         "rtos", "rtos_version", "firmware_sha256", "transport",
         "single_debugger_owner", "no_unapproved_debug_actions",
         "measurement", "attachments"},
    )
    if profile["schema"] != SCHEMA:
        raise EvidenceError("unsupported W19 schema")
    origin = profile["sample_origin"]
    if origin not in ("synthetic", "physical"):
        raise EvidenceError("sample_origin must be synthetic or physical")
    for key in ("board", "silicon", "probe", "rtos", "rtos_version"):
        identifier(profile[key], key)
    firmware = profile["firmware_sha256"]
    if not isinstance(firmware, str) or not HEX_SHA.fullmatch(firmware):
        raise EvidenceError("firmware identity: invalid SHA256")
    if profile["transport"] not in SUPPORTED:
        raise EvidenceError("unqualified/unknown transport")
    owner = boolean(profile["single_debugger_owner"], "single_debugger_owner")
    safe_debug = boolean(
        profile["no_unapproved_debug_actions"], "no_unapproved_debug_actions")
    m = check_object(
        profile["measurement"], "measurement",
        {"cpu_hz", "baseline_cycles", "instrumented_cycles",
         "observed_events", "elapsed_ms", "wire_bytes",
         "full_drops", "busy_drops", "ram_bytes", "flash_bytes"},
    )
    cpu_hz = integer(m["cpu_hz"], "cpu_hz", 1, 2**40)
    baseline = samples(m["baseline_cycles"], "baseline_cycles")
    instrumented = samples(m["instrumented_cycles"], "instrumented_cycles")
    if len(baseline) != len(instrumented):
        raise EvidenceError("baseline and instrumented samples must be paired")
    events = integer(m["observed_events"], "observed_events", 1)
    ms = integer(m["elapsed_ms"], "elapsed_ms", 1)
    byte_count = integer(m["wire_bytes"], "wire_bytes")
    full = integer(m["full_drops"], "full_drops")
    busy = integer(m["busy_drops"], "busy_drops")
    ram = integer(m["ram_bytes"], "ram_bytes")
    flash = integer(m["flash_bytes"], "flash_bytes")
    raw = profile["attachments"]
    if not isinstance(raw, list) or len(raw) > 20:
        raise EvidenceError("attachments: list limit exceeded")
    kinds = [attachment(root, a) for a in raw]
    if len(kinds) != len(set(kinds)):
        raise EvidenceError("attachment kinds must be unique")
    missing = sorted(REQUIRED_ATTACHMENTS - set(kinds))
    if profile["transport"] == "RTT" and "rtt_ownership_qualification" not in kinds:
        missing.append("rtt_ownership_qualification")
    if not owner:
        missing.append("single_debugger_owner")
    if not safe_debug:
        missing.append("no_unapproved_debug_actions")
    if origin != "physical":
        missing.append("physical_target_measurement")
    # Negative measurements are legitimate: do not clip them or call them
    # evidence of target improvement. These are input-reported counts.
    differences = [after - before for before, after in zip(baseline, instrumented)]
    baseline_median = statistics.median(baseline)
    instrumented_median = statistics.median(instrumented)
    difference_median = statistics.median(differences)
    report = {
        "schema": SCHEMA,
        "board": profile["board"],
        "rtos": profile["rtos"],
        "transport": profile["transport"],
        "source": origin,
        "hardware_qualification": "DEFERRED" if origin == "synthetic"
                                  else "REQUIRES_INDEPENDENT_HUMAN_REVIEW",
        "structural_readiness": "EVIDENCE_PRESENT_REVIEW_REQUIRED" if not missing
                                else "INCOMPLETE",
        "missing_evidence": sorted(missing),
        "measurements_unverified": {
            "cpu_hz": cpu_hz,
            "samples": len(baseline),
            "baseline_cycles_median": baseline_median,
            "instrumented_cycles_median": instrumented_median,
            "paired_overhead_cycles_median": difference_median,
            "paired_overhead_us_median": difference_median * 1000000 / cpu_hz,
            "observed_event_rate_per_s": events * 1000 / ms,
            "observed_wire_bytes_per_s": byte_count * 1000 / ms,
            "declared_full_drops": full,
            "declared_busy_drops": busy,
            "reported_recorder_ram_bytes": ram,
            "reported_recorder_flash_bytes": flash,
        },
        "notes": [
            "Calculated from submitted numbers, not verified against physical equipment",
            "Observed rate is not maximum sustained bandwidth or throughput",
            "No tail-loss absence, accurate CPU percentage, or ISR WCET proof",
            "Human review must reconcile independent timing, probe logs and firmware identity",
        ],
    }
    return report


def read_manifest(path: Path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_JSON:
        raise EvidenceError("manifest unavailable/symlink/oversized")
    with path.open("rb") as file:
        content = file.read(MAX_JSON + 1)
    try:
        data = json.loads(content)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise EvidenceError("manifest invalid JSON/encoding") from exc
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    arguments = parser.parse_args(argv)
    try:
        root = arguments.manifest.parent
        report = summarize(read_manifest(arguments.manifest), root)
        result = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if arguments.report:
            # Never replace existing audit evidence.
            if arguments.report.is_symlink():
                raise EvidenceError("report destination may not be a symlink")
            with arguments.report.open("x", encoding="utf-8", newline="\n") as out:
                out.write(result)
        else:
            sys.stdout.write(result)
        return 0
    except (EvidenceError, OSError) as exc:
        print(f"W19 evidence rejected: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
