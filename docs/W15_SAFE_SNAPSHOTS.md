<!-- SPDX-License-Identifier: Apache-2.0 -->
# W15 — Selective safe memory, register and TCB snapshot acquisition

**Package status:** Source implementation and software regression tests only; open PR. **Eclipse PDE/Tycho and real probe/MCU validation have NOT run**, so production read capability and safety are not qualified. Hardware qualification remains deferred to W19.

## Ownership and security

```text
Selected EXISTING Eclipse CDT / DSF-GDB session
    -> explicit one-shot user consent (<=30 seconds)
    -> qualified board RAM allowlist + all-core stopped verifier
    -> SAME owning DSF executor rechecks isSuspended + all cores
    -> bounded IMemory / IRegisters READ ONLY
    -> atomic local UNVERIFIED receipt in private output directory
    -> RTOS Observatory read-only Receipt Studio (no target connection)
```

The native C++20 policy in `include/observatory/snapshot_acquisition.hpp` is an independent, deterministic **decision model**, not a remote execution API. A false self-reported W14 `SUSPENDED` value is never sufficient to approve a read. Only explicit owning-DSF state checks, independently qualified all-core stopped status, per-operator consent and read-safe RAM ranges may permit the Java adapter to start.

**Denied by default:** running or partially suspended multicore targets; unknown debug ownership/capabilities; no or expired consent; reused approvals (including failed attempts); MMIO/peripherals, flash and unsafe memory classes; unknown register names; unbounded reads; cross-region spans, 32-bit overflow, writes, reset, stepping, resume, halt, flash, watchpoints and a second GDB/OpenOCD connection.

### Hard limits

- 1 explicit consent, 1 Eclipse DSF session, 1 epoch, 1 snapshot.
- Up to **32** operations, **256 bytes per RAM/TCB read**, **4096 bytes total**, **16** named general CPU registers; read-safe RAM maps only.
- The C++ policy expects a 30-second expiry maximum. Eclipse adapter requires an existing private `RTOS_OBSERVATORY_BRIDGE_DIR`, validates the selected DSF execution and memory contexts, and checks the all-core verification callback **before each read and before receipt emission**.
- CPU register names are a fixed set: PC, SP, LR, R0–R12, XPSR, MSP, PSP. No unsolicited enumeration of peripheral or special status registers.
- TCB is just **operator-approved raw bytes in RAM**. No RTOS-version-specific field offsets, scheduler state or kernel object layout is guessed.
- The Java source adapter lives at `bridge/eclipse/DsfApprovedSnapshotCapture.java`. It is **not yet wired to an installed ZEW UI command**. Its `Consent` and `QualifiedRamRegion` objects must be created only by the owning UI after genuine user confirmation and board/DTS+memory-map qualification; caller **must not fake** `allCoresQualifiedSuspended`. A qualified owning-DSF `BooleanSupplier` must reverify every core on each step. There is intentionally no autonomous or default acquisition.

**Access side effects:** A debugger read is not always safe. The Java adapter only accepts explicitly qualified read-safe RAM regions; even apparent RAM mappings on an SoC may require vendor review. Real-board operations cannot be marked safe from synthetic tests.

## Receipt v1 — explicit unknowns

The writer produces a local tab-delimited, LF-terminated file via atomic replacement (private directory). Header:

```text
RTOS-OBSERVATORY-APPROVED-SNAPSHOT<TAB>1<TAB>session<TAB>epoch<TAB>approval-id<TAB>source<TAB>32<TAB>unverified<LF>
```

Each `MEM`/`TCB` row contains label, hexadecimal RAM address, requested byte count, `ok|failed`, and **exact hex bytes or `-`**. Each `REG` row has address `0x0`, size `0`, `ok|failed`, and an exact hex value or `-`. Failed reads never turn into zero; the snapshot receipt does not assert that all cores were coherently sampled. Maximum receipt: 16 KiB, 1024 bytes per line, 32 operations. Actual bytes remain private to the chosen local directory and user.

The `include/observatory/snapshot_receipt.hpp` reader rejects bad record shape, missing LF, unknown state, source control characters, memory value mismatch, over-budget ranges and read-status contradictions. The native **Approved snapshot receipts** GUI shows successful/failed samples and first 16 recorded bytes, but **cannot request any target read**. CLI:

```sh
rtos-observatory-desktop --snapshot-receipt examples/approved-snapshot.tsv
```

The supplied fixture is **synthetic/unverified**, not a real captured read.

## Software tests

- `observatory_snapshot_permission_policy`: owner, user consent, all-core stop, stale grant, replayed one-shot ID, RAM region boundaries, MMIO/flash rejects, fixed CPU registers, read budgets, unknown data and incoherent snapshot refusal.
- `observatory_snapshot_receipt_bounds`: known-answer offline receipt, malformed/failure/truncated sample data, overflowing addresses and caps.
- `observatory_desktop_snapshot_smoke`: headless GUI-model receipt import.
- `observatory_imgui_snapshot_ui_smoke`: Linux Xvfb hidden GLFW/ImGui render.
- Windows and Linux native/desktop Debug+Release and Windows clang-cl Debug+Release remain PR qualification gates.

### Outstanding hard blockers

1. The Java source must be imported into the actual Zephyr Eclipse Workbench UI bundle, wired to a **non-automatic, explicit user approval form**, with **qualified** Zephyr/SoC RAM read-safe ranges and an owning-DSF all-core verification provider.
2. ZEW PDE/Tycho build, installed-plugin test and mock/real DSF service read tests have **not** run; source API compatibility is not accepted.
3. Board-specific target reads, multi-core coherency and read-side-effect audits are **deferred to W19**. The current C++/GUI/synthetic tests cannot prove physical safety, correctness or performance.

W15 is not real-time trace transport (W17–W18), not a qualified vendor RTOS TCB interpreter (W20+), and not permission to change hardware debug state.
