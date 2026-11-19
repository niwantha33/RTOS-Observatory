<!-- SPDX-License-Identifier: Apache-2.0 -->
# W19 — MSPM0G3507/XDS110 hardware qualification kit

**Status: software qualification tooling only. Hardware qualification DEFERRED.**
W18 GUI and source-code changes are merged on main, but this is not evidence that a physical MSPM0G3507 or an installed Eclipse CDT/DSF plugin works. The first target is TI MSPM0G3507 LaunchPad with XDS110; Zephyr + TI OpenOCD are *intended* contexts, not automatically verified operating configurations.

## Evidence required for physical qualification (later)

1. Record exact run identity, board revision, silicon, probe firmware, OpenOCD/debug-server version, Zephyr/other RTOS version and firmware SHA256, separately from the host OS/toolchain. No mismatched ELF/firmware/trace identities.
2. Demonstrate **single debugger/probe owner**: an already-running Zephyr Eclipse Workbench DSF session controls XDS110; Observatory must neither open an independent GDB/OpenOCD probe nor issue unapproved halt/resume/flash/reset operations. Save original probe/server logs. If coexistence cannot be demonstrated, record BLOCKED.
3. Capture **unmodified raw W17 trace** and actual recorder W16 full/busy/per-lane attempts. Include a declared start/end epoch; if the stream ends without a tail marker, trailing loss remains unknown. RTT requires separate explicit ownership and safe-running-target qualification; do not assume memory reads or RTT exist.
4. Record **independent timing** (logic analyzer or oscilloscope) alongside a trace-disabled baseline. Match board clock, test workload, voltage/temperature and capture interval; save at least three paired samples. Label this a measured sample, never theoretical worst-case WCET.
5. Record \`map\`/linker RAM + flash footprint; compiler flags/instrumentation code and per-context stack safety. Measure host wire bytes, observed throughput, missing frames/attempts, sender backpressure and device drops.
6. Record failure behavior under overflow, reconnect, reset, stalled transmission and task/ISR contention. Keep unknown state unknown; test without disabling the debugger or causing unapproved target state changes.
7. Independently review/approve resulting evidence. **No script can attest genuine hardware merely because a JSON key says "physical".** The board verdict requires independent human/physical review with identifiable source artifacts, and the tracker stays unqualified until that is done.

## Source-only evidence parser

\`tools/w19_qualification.py\` validates a bounded JSON manifest (\`rtos-observatory-w19-v1\`), checks exact file SHA256 of attached local artifacts, produces reproducible **unverified arithmetic** and a fail-closed completeness report.

**Allowed origins:** \`synthetic\` and \`physical\`. Synthetic always reports \`hardware_qualification: DEFERRED\`. A structurally complete physical manifest can only report \`REQUIRES_INDEPENDENT_HUMAN_REVIEW\`, **never PASSED**, even when every file digest checks out. A malformed manifest, incorrect SHA256, local path traversal, symlink, unknown transport, unsupported schema or oversized file is rejected.

Run with the shipped synthetic example (does not require a board):

\`\`\`powershell
python .\tools\w19_qualification.py --manifest .\examples\w19-synthetic.json
# Explicitly creates a NEW file; will never overwrite an existing report:
python .\tools\w19_qualification.py --manifest .\examples\w19-synthetic.json --report .\w19-readiness.json
\`\`\`

The example's cycle counts, clock, firmware SHA256 and identities are **invented test values**, not measurements. Output values such as \`paired_overhead_us_median\` and \`observed_event_rate_per_s\` are calculated from those inputs only. They are NOT measured MCU throughput, validated clocks, ISR WCET or guarantees about any board.

### V1 manifest shape

\`\`\`json
{
  "schema": "rtos-observatory-w19-v1",
  "sample_origin": "physical",
  "run_id": "site-run-001",
  "board": "lp_mspm0g3507",
  "board_revision": "revA",
  "silicon": "MSPM0G3507",
  "probe": "XDS110",
  "probe_firmware": "exact-version",
  "debug_server": "TI-OpenOCD-exact-version",
  "rtos": "Zephyr",
  "rtos_version": "exact-version",
  "firmware_sha256": "64-lowercase-hex-character-identity",
  "transport": "UART",
  "single_debugger_owner": true,
  "no_unapproved_debug_actions": true,
  "measurement": {
    "cpu_hz": 32000000,
    "baseline_cycles": [100, 110, 120],
    "instrumented_cycles": [130, 150, 165],
    "observed_events": 50,
    "elapsed_ms": 1000,
    "wire_bytes": 2400,
    "full_drops": 3,
    "busy_drops": 2,
    "ram_bytes": 512,
    "flash_bytes": 4096
  },
  "attachments": [
    {"kind":"firmware_map","path":"map.txt","sha256":"exact-64-hex-digest"},
    {"kind":"probe_log","path":"openocd.log","sha256":"exact-64-hex-digest"},
    {"kind":"independent_timing","path":"scope.csv","sha256":"exact-64-hex-digest"},
    {"kind":"raw_trace","path":"raw.rtob","sha256":"exact-64-hex-digest"},
    {"kind":"baseline_measurement","path":"baseline.csv","sha256":"exact-64-hex-digest"}
  ]
}
\`\`\`

This is a *schema illustration*, not a valid runnable manifest: real SHA256 digests, real local files, exact versions and independent hardware evidence must replace placeholders. A verified hash only detects file alteration relative to the manifest, **not proof of when or where the file originated**. \`single_debugger_owner\` and \`no_unapproved_debug_actions\` are self-reported values and must be independently checked. RTT additionally requires an \`rtt_ownership_qualification\` artifact.

### Metrics, limits and interpretation

- At least 3 and at most 1024 paired **baseline vs instrumented cycle samples**, with the same sampling method and workload. Pairwise median difference is computed, and converted to microseconds using **reported** CPU frequency (not independently proved). Negative differences are retained, not hidden.
- An observed event rate is \`observed_events × 1000 / elapsed_ms\`; this is **not maximal throughput** or lossless capacity. Reported full/busy drops are not silently summed with wire missing-frame/attempt gaps, which may overlap.
- Manifest ≤128 KiB; up to 20 local attachments, each ≤8 MiB; bounded file reading and SHA256 hashing. No network fetches, no target I/O or debugger commands, and output file creation uses exclusive create.
- Standalone Python stdlib; \`observatory_w19_evidence_safety\` CTest covers synthetic no-pass, a physically-labelled but still human-review-only case, malformed fields, forged hash, path traversal, report overwrite protection and oversized input.

## W19 acceptance and blockers

**Software kit completed only when CI tests pass** (Windows/Linux CTest). There is **no simulated hardware PASS**. Physical test remains **DEFERRED** until a user/operator attaches real target evidence and independent timing, reviews debug ownership, and signs off. The project may proceed with independently gated nonhardware packages under the agreed deferred-hardware policy, without relabeling this package as fully qualified. Do not merge an incomplete hardware verdict into a release claim.
