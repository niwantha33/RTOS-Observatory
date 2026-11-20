# SPDX-License-Identifier: Apache-2.0
"""W19 source-only qualification: known arithmetic and fail-closed provenance."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import w19_qualification as w  # noqa: E402


def demo_wire():
    event = struct.pack("<QIIHHIHH", 1000, 42, 7, 1, 0, 1, 0, 1)
    stats = struct.pack("<HHIIIII", 0, 0, 50, 1, 3, 2, 0)
    def frame(kind, index, payload):
        header = struct.pack("<4sBBHII", b"RTOB", 1, kind,
                             len(payload), 1, index)
        return header + payload + struct.pack("<I", zlib.crc32(header+payload))
    return frame(1,1,event)+frame(2,2,stats)


def demo(root):
    evidence = []
    for kind in sorted(w.REQUIRED_ATTACHMENTS):
        path = root / (kind + ".txt")
        raw = demo_wire() if kind == "raw_trace" else (
            kind + ": synthetic-only\n").encode()
        path.write_bytes(raw)
        evidence.append({
            "kind": kind, "path": path.name,
            "sha256": hashlib.sha256(raw).hexdigest(),
        })
    return {
        "schema": w.SCHEMA,
        "sample_origin": "synthetic",
        "run_id": "W19-SYNTHETIC-001",
        "board": "lp_mspm0g3507",
        "board_revision": "revA",
        "silicon": "MSPM0G3507",
        "probe": "XDS110",
        "probe_firmware": "3.0.0.41",
        "debug_server": "TI_OpenOCD_0.12",
        "rtos": "Zephyr",
        "rtos_version": "4.4.0",
        "firmware_sha256": "f"*64,
        "transport": "UART",
        "single_debugger_owner": True,
        "no_unapproved_debug_actions": True,
        "measurement": {
            "cpu_hz": 32000000,
            "baseline_cycles": [100, 110, 120],
            "instrumented_cycles": [130, 150, 165],
            "observed_events": 1,
            "elapsed_ms": 1000,
            "wire_bytes": len(demo_wire()),
            "full_drops": 3,
            "busy_drops": 2,
            "ram_bytes": 512,
            "flash_bytes": 4096,
        },
        "attachments": evidence,
    }


def refuses(root, source):
    try:
        w.summarize(source, root)
    except w.EvidenceError:
        return
    raise AssertionError("invalid W19 evidence was accepted")


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = demo(root)
        report = w.summarize(source, root)
        assert report["hardware_qualification"] == "DEFERRED"
        assert report["structural_readiness"] == "INCOMPLETE"
        assert "physical_target_measurement" in report["missing_evidence"]
        n = report["measurements_unverified"]
        assert n["samples"] == 3
        assert n["paired_overhead_cycles_median"] == 40
        assert abs(n["paired_overhead_us_median"]-1.25) < 1e-9
        assert n["observed_event_rate_per_s"] == 1
        assert n["observed_wire_bytes_per_s"] == 92
        assert report["validated_wire_observations"]["frames"] == 2
        assert report["validated_wire_observations"]["events"] == 1
        assert report["validated_wire_observations"]["tail_loss_unknown"]
        assert n["declared_full_drops"] == 3
        assert n["declared_busy_drops"] == 2

        physical = copy.deepcopy(source)
        physical["sample_origin"] = "physical"
        physical_report = w.summarize(physical, root)
        assert physical_report["structural_readiness"] == "EVIDENCE_PRESENT_REVIEW_REQUIRED"
        assert physical_report["hardware_qualification"] == "REQUIRES_INDEPENDENT_HUMAN_REVIEW"
        assert "PASSED" not in json.dumps(physical_report)

        rtt = copy.deepcopy(physical)
        rtt["transport"] = "RTT"
        assert "rtt_ownership_qualification" in w.summarize(rtt, root)["missing_evidence"]

        no_owner = copy.deepcopy(physical)
        no_owner["single_debugger_owner"] = False
        assert "single_debugger_owner" in w.summarize(no_owner, root)["missing_evidence"]

        invalid = copy.deepcopy(source)
        invalid["measurement"]["baseline_cycles"] = [1, 2]
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["measurement"]["instrumented_cycles"] = [1, 2, 3, 4]
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["measurement"]["observed_events"] = 99
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["measurement"]["wire_bytes"] = 0
        refuses(root, invalid)

        # A discrepancy in noncoherent firmware-drop reports downgrades
        # readiness rather than inventing complete loss accounting.
        invalid = copy.deepcopy(source)
        invalid["measurement"]["full_drops"] = 99
        assert "unreconciled_drop_counter_claims" in (
            w.summarize(invalid,root)["missing_evidence"])

        invalid = copy.deepcopy(source)
        invalid["measurement"]["cpu_hz"] = True
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["measurement"]["cpu_hz"] = 0
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["transport"] = []
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["transport"] = "GDB_OPENOCD_RAW"
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["firmware_sha256"] = "not-a-hash"
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["attachments"][0]["path"] = "../outside.txt"
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["attachments"][0]["path"] = str(root.resolve() / "abs.txt")
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["attachments"][0]["sha256"] = "a"*64
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["attachments"].append(copy.deepcopy(invalid["attachments"][0]))
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["unexpected"] = 5
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["sample_origin"] = "certified"
        refuses(root, invalid)

        invalid = copy.deepcopy(source)
        invalid["measurement"]["baseline_cycles"] = list(range(1, w.MAX_SAMPLES+2))
        refuses(root, invalid)

        # The CLI writes new reports only; never overwrites independent evidence.
        manifest = root / "manifest.json"
        output = root / "report.json"
        manifest.write_text(json.dumps(source), encoding="utf-8")
        cmd = [
            sys.executable, str(ROOT / "tools" / "w19_qualification.py"),
            "--manifest", str(manifest), "--report", str(output),
        ]
        run = subprocess.run(cmd, check=False, capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
        saved = json.loads(output.read_text(encoding="utf-8"))
        assert saved["hardware_qualification"] == "DEFERRED"
        again = subprocess.run(cmd, check=False, capture_output=True, text=True)
        assert again.returncode == 2
        assert output.read_text(encoding="utf-8") == json.dumps(
            saved, indent=2, sort_keys=True) + "\n"

        # A forged file hash is not counted as physical timing evidence.
        (root / "independent_timing.txt").write_text("mutated artifact", encoding="utf-8")
        checksum = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "w19_qualification.py"),
             "--manifest", str(manifest)],
            check=False, capture_output=True, text=True,
        )
        assert checksum.returncode == 2
        assert "SHA256 mismatch" in checksum.stderr
        # Restore the original attachment so the next rejection truly tests
        # the W17 frame CRC, not a stale unrelated SHA256 failure.
        (root / "independent_timing.txt").write_bytes(
            b"independent_timing: synthetic-only\n")

        # Even if a corrupted raw trace is rehashed into the manifest,
        # the independent binary CRC check must still reject it.
        raw_trace = root / "raw_trace.txt"
        good_trace = raw_trace.read_bytes()
        tampered = bytearray(good_trace)
        tampered[25] ^= 0x20
        raw_trace.write_bytes(tampered)
        forged = copy.deepcopy(source)
        next(item for item in forged["attachments"]
             if item["kind"] == "raw_trace")["sha256"] = (
                 hashlib.sha256(tampered).hexdigest())
        refuses(root, forged)
        raw_trace.write_bytes(good_trace)

        # W19 byte-level replay/truncation and declared missing-frame gaps.
        import w19_wire_audit as wire
        assert wire.audit_trace(raw_trace)["frames"] == 2
        raw_trace.write_bytes(good_trace[:-1])
        try:
            wire.audit_trace(raw_trace)
        except wire.WireAuditError:
            pass
        else:
            raise AssertionError("truncated physical wire capture accepted")
        raw_trace.write_bytes(good_trace + good_trace[:48])
        try:
            wire.audit_trace(raw_trace)
        except wire.WireAuditError:
            pass
        else:
            raise AssertionError("replayed physical wire capture accepted")
        raw_trace.write_bytes(good_trace)
        # Modify the stats frame's transport index from 2 to 3, then
        # recalculate its CRC so the gap is genuine, not a checksum error.
        gapped=bytearray(good_trace)
        gapped[48+12:48+16]=struct.pack("<I",3)
        gapped[-4:]=struct.pack("<I",zlib.crc32(gapped[48:-4]))
        raw_trace.write_bytes(gapped)
        audit=wire.audit_trace(raw_trace)
        assert audit["missing_wire_frames"]==1
        assert audit["tail_loss_unknown"]
        raw_trace.write_bytes(good_trace)

        # The manifest itself is bounded and cannot be a symlink.
        large = root / "oversized.json"
        large.write_bytes(b"x"*(w.MAX_JSON+1))
        try:
            w.read_manifest(large)
        except w.EvidenceError:
            pass
        else:
            raise AssertionError("oversized manifest accepted")

    print("W19 synthetic-only audit, independent-review gate and hostile evidence tests passed")


if __name__ == "__main__":
    main()
