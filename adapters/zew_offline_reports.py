#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Import existing Zephyr Workbench saved-CTF aggregate JSON reports.

This is a read-only REPORT importer, NOT a raw CTF decoder or a substitute
for scheduler event-stream conversion. It preserves Scheduled-vs-exclusive
Running distinctions and never invents object ownership timestamps.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

MAX_FILE = 30 * 1024 * 1024
MAX_ROWS = 100000


def load_report(path: Path, expected_kind: str) -> dict:
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_FILE:
        raise ValueError(f"invalid/oversized report file: {path}")
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict) or data.get("schema_version") != 1 or data.get("kind") != expected_kind:
        raise ValueError(f"expected ZEW {expected_kind} JSON schema_version 1")
    return data


def int_us(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 2**63 - 1:
        raise ValueError("invalid nanosecond timestamp/duration")
    return value // 1000


def text(value: object) -> str:
    # Text is opaque presentation data; never treat it as HTML/JS.
    return str(value or "")[:256]


def convert(cpu: dict, contention: dict | None = None) -> dict:
    if cpu.get("kind") != "cpu" or cpu.get("schema_version") != 1:
        raise ValueError("invalid CPU report")
    start_ns = cpu.get("start_ns")
    end_ns = cpu.get("end_ns")
    if start_ns is None or end_ns is None or int_us(end_ns) <= int_us(start_ns):
        raise ValueError("CPU report has no valid observed time window")
    start_us, end_us = int_us(start_ns), int_us(end_ns)
    details = cpu.get("cpu")
    if not isinstance(details, dict):
        raise ValueError("missing CPU details")
    states = details.get("thread_states")
    if not isinstance(states, list) or len(states) > MAX_ROWS:
        raise ValueError("invalid/oversized CPU state rows")
    warnings = ["Imported from aggregate ZEW saved-CTF CPU report: not a raw event stream; no independent hardware attestation.",
                "Microsecond floor conversion discards sub-microsecond precision; Scheduled intervals may include ISR time."]
    if cpu.get("reason"):
        warnings.append("ZEW CPU: " + text(cpu.get("reason")))
    incomplete = bool(cpu.get("incomplete")) or cpu.get("status") != "ok"
    intervals = []
    thread_ids = set()
    for row in states:
        if not isinstance(row, dict):
            raise ValueError("non-object thread interval")
        name = text(row.get("name"))
        state = text(row.get("state"))
        if not name or state not in ("Scheduled", "Ready", "Blocked", "Sleeping", "Unknown"):
            raise ValueError("unexpected/unnamed thread state")
        begin, finish = int_us(row.get("start_ns")), int_us(row.get("end_ns"))
        if not start_us <= begin < finish <= end_us:
            # Incomplete/zero-duration segments have no drawable interval.
            incomplete = True
            warnings.append("Discarded an interval with invalid/missing rounded bounds")
            continue
        thread_ids.add(name)
        intervals.append({"task": name, "core": "unspecified", "state": state.lower(),
                          "start_us": begin, "end_us": finish})
    findings = []
    if contention is not None:
        if contention.get("kind") != "contention" or contention.get("schema_version") != 1:
            raise ValueError("invalid contention report")
        if bool(contention.get("incomplete")) or contention.get("status") != "ok":
            incomplete = True
            warnings.append("ZEW contention report is incomplete")
        rows = contention.get("findings", [])
        if not isinstance(rows, list) or len(rows) > MAX_ROWS:
            raise ValueError("invalid/oversized contention findings")
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("non-object contention finding")
            task = text(row.get("waiter")) or "unknown"
            object_name = text(row.get("object")) or "unknown"
            duration = int_us(row.get("duration_ns")) if row.get("duration_ns") is not None else 0
            findings.append({"code": "ZEW_" + text(row.get("kind")).upper().replace(" ", "_")[:50],
                             "confidence": "imported-aggregate", "description": text(row.get("explanation")),
                             "task": task, "resource": object_name,
                             "start_us": 0, "end_us": 0, "observed_us": duration})
        if rows:
            warnings.append("Imported contention findings have no event timestamps; they cannot be positioned on the timeline.")
    return {"schema": "rtos-observatory-report/v1", "session": "zephyr-saved-ctf-report",
            "source": "imported-zew-offline-aggregate", "complete": not incomplete,
            "start_us": start_us, "end_us": end_us,
            "threads": [{"id": name, "priority": None, "period_us": None, "idle": False}
                        for name in sorted(thread_ids)],
            "intervals": intervals, "waits": [], "cycles": [],
            "findings": findings, "warnings": warnings}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cpu", type=Path, required=True, help="ZEW offline_trace_json.py --mode cpu output")
    p.add_argument("--contention", type=Path, help="Optional --mode contention output from the same CTF")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.cpu.resolve() == args.output.resolve() or (args.contention and args.contention.resolve() == args.output.resolve()):
        p.error("Output may not overwrite an input report")
    try:
        result = convert(load_report(args.cpu, "cpu"),
                         load_report(args.contention, "contention") if args.contention else None)
        args.output.write_text(json.dumps(result, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as exc:
        p.error(str(exc))
    print(f"Imported {len(result['intervals'])} observed intervals, {len(result['findings'])} aggregate findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
