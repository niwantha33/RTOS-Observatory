<!-- SPDX-License-Identifier: Apache-2.0 -->
# W12 — Offline fault, TCB, stack, heap and saved memory evidence

**Status:** PR under review. **Scope:** a bounded read-only, locally supplied, *versioned snapshot*, independent of the normalized RTOS event trace. No debugger/probe attached; no hardware has been qualified.

## Safe input contract

The text file uses **literal tabs**, one record per line, UTF-8/ASCII identifiers and hexadecimal unsigned values. A literal `-` means **missing / failed**, never the numeric value zero. Lines starting `#` are comments. Records retain the physical source line number for later investigation.

First line:

```text
RTOS-OBSERVATORY-FAULT<TAB>1<TAB>architecture<TAB>address-bits<TAB>firmware-label<TAB>capture-id<TAB>coherence
```

- Address width is exactly **32** or **64**; coherence is `unverified` or `claimed-coherent`. Both are **self-declared**, never independent device/firmware attestation.
- Supported structural architecture labels are arbitrary tokens; **CFSR/HFSR bit decoding** is limited to explicitly recognized 32-bit Cortex-M3/M4/M7/M33/M35P/M55/M85 labels. Armv8-M-only `STKOF` (CFSR bit 20) is decoded only on the explicitly named Armv8-M profiles, not Cortex-M4. The decoder does not claim all bits are implemented by every specific SoC.
- Hex addresses and byte counts accept `0x` prefix, and the binary reader rejects overflow, width mismatch, ambiguous overlaps, bad line shape and oversized records.

Record formats:

| Record | Fields after keyword | Evidence represented |
| --- | --- | --- |
| `REGION` | name, `ram/flash/peripheral/other`, base, size, `r/rw/rx/rwx/none` | **Declared** memory map; source-controlled, not reconstructed |
| `THREAD` | task, TCB address, TCB bytes, stack base, stack bytes, observed SP/`-`, reported stack-used bytes/`-` | Explicit pointer + allocation metadata; **not** an OS-specific TCB layout |
| `HEAP` | name, base, capacity, reported used/`-`, reported peak/`-` | External allocator counters; no inferred allocation graph |
| `REG` | context, register name, value/`-`, `ok/failed` | Actual supplied register record or explicitly failed read |
| `FAULT` | marker type, context, address/`-` | Source-reported exception/fault marker, unverified |
| `MEM` | region, address, hex byte string/`-`, `ok/failed` | **Saved** read bytes only, never a new target read |

A `MEM` sample is limited to 512 bytes; total bytes are limited to 64 KiB; input to 1 MiB and 4096 bytes/line. Default caps also apply to regions (128), tasks/heaps (1024 each), registers/fault markers (2048), memory records (256), findings (4096). The parser refuses duplicates and ambiguous overlapping REGION declarations.

## Findings, with proof boundaries

- **TCB / stack / heap map:** compares explicit source-provided allocation ranges against explicit RAM map records; absent RAM maps are **unknown**, not invalid.
- **SP outside declared stack:** identifies a metadata/register mismatch, **not** proved stack overflow. Source-reported stack-used and heap used/peak counters are checked only for range consistency.
- **PC outside executable map:** only when executable map entries exist; an incomplete map can explain the discrepancy.
- **Cortex-M CFSR/HFSR flags:** reads only explicitly supplied register values, maps documented bits when the architecture label qualifies, and preserves source lines. A status flag is **not a root-cause proof**.
- **Failed register/memory reads:** remain missing, not zero; no raw memory scanning, disassembly, intrusive peripheral reads, exception-frame guessing, unwinding, RISC-V decoding, or assumed RTOS TCB layout.
- **Peripheral samples:** recorded bytes may be displayed locally but are never interpreted as safe live peripheral reads.

The analyzer never certifies coherency, provenance, RTOS identity or firmware identity. The only time relationship is that asserted by the source manifest. No ARM exception-frame read is attempted automatically.

### Local desktop workflow

```powershell
rtos-observatory-desktop.exe --fault-snapshot examples/fault-snapshot.tsv
```

Or select **Fault & memory** in the app sidebar and enter a local file path; drag-and-drop also recognizes `.fault.tsv` / `fault-snapshot.tsv`. You can browse declared memory regions, task TCB/stack records, heap statistics, register values, memory byte previews and evidence-linked findings. Each finding references **physical snapshot line numbers**; the UI copies a `path:line` reference, not an invented link to the separate RTOS trace.

**Synthetic demonstration:** `examples/fault-snapshot.tsv` shows recorded HardFault, Cortex-M CFSR/HFSR bits, a mismatched SP, inconsistent stack/heap reported usage, missing register/memory reads, and a supplied executable map. It is deliberately **not** captured from an MCU.

### Qualification and future integration

- `observatory_fault_evidence`: self-contained offline model/parser with known-answer Cortex-M, RAM/TCB/stack/heap, missing/failed reads, overflow, schema and budget tests, plus 64-bit edge cases.
- `observatory_desktop_fault_smoke`: load the fixture and verify the model without GLFW/graphics.
- `observatory_imgui_fault_ui_smoke`: render the independent Fault & memory panel on Linux using Xvfb and OpenGL.
- Required before acceptance: native Windows/Linux Debug+Release, desktop Windows/Linux Debug+Release, clang-cl Debug+Release, independent review, PR merge.
- Future **W14/W15** must provide a qualified debugger bridge, access permissions, read-side-effect safety, snapshot coherence assessment, architecture identity and a device-specific typed TCB adapter. W13 will handle actual symbol/source mapping.

Reference for fault-bit semantics (not hardware proof): [Arm Cortex-M4 Generic User Guide](https://documentation-service.arm.com/static/5f2ac4ab60a93e65927bbdbf), [Armv8-M exception model](https://support.arm.com/documentation/107706/latest/System-exceptions/Fault-exceptions-and-their-causes/Usage-faults).
