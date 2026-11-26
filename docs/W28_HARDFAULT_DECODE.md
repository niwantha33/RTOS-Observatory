<!-- SPDX-License-Identifier: Apache-2.0 -->
# W28 — offline supplied-frame Cortex-M HardFault decoder

**Status:** draft on branch `feature/w28-cortexm-hardfault-decode`; not merged.
**Scope:** a conservative offline interpretation of an externally supplied W12
fault snapshot that additionally contains a saved Cortex-M exception stack
frame. No debugger, probe, target read, disassembly or frame reconstruction is
performed. Every output is an **observation tied to supplied source lines**; a
finding never claims a proved root cause.

## Motivation: the universal embedded debugging gap

The most frequent field escalation in bare-metal firmware is a device landing
in `HardFault_Handler` with only program counter, status registers and a stack
dump. Existing tooling usually shows raw registers; it does not conservatively
answer, from evidence, *which fault class escalated, which address was reported,
whether the frame fit the recorded stack, and which supplied function owned the
crashed PC*. W28 addresses that workflow inside the Observatory evidence model.

## Safe input contract (additive to W12 schema 1)

The decoder consumes the existing `RTOS-OBSERVATORY-FAULT 1` file and two new
optional record kinds. Unknown records remain rejected; old fixtures keep
parsing unchanged.

| Record | Fields after keyword | Evidence represented |
| --- | --- | --- |
| `FRAME` | context, stacked SP/`-`, `basic`\|`fpu`, R0, R1, R2, R3, R12, LR, PC, xPSR (hex/`-`) | One saved exception frame supplied by the source. LR holds the stacked `EXC_RETURN`. Missing words stay unknown. |
| `SYMB` | address, size, function name | Read-only supplied function table for PC symbolication; image identity is not attested. |

Bounds: at most 60 frames and 4096 symbols by default (`FaultLimits.max_frames`,
`max_symbols`); widths are checked against the declared 32/64-bit address bus;
duplicate frame contexts and duplicate symbol tuples are rejected. A standalone
flat symbol file (`address<TAB>size<TAB>name`, `#` comments) is accepted via
`--symbols`; rows beyond its limits or with non-hex cells are rejected.

## What the decoder establishes

1. **EXC_RETURN interpretation** from the stacked LR: Thread/Handler mode,
   MSP/PSP selection, and whether a floating-point extended frame is indicated.
   A bit pattern outside the reserved-bit contract yields `EXC_RETURN_MALFORMED`;
   a declared `fpu` frame that disagrees with the EXC_RETURN FPU bit yields
   `FRAME_FORMAT_MISMATCH`.
2. **CFSR/HFSR fault classes** from supplied register values:
   - MemManage (`IACCVIOL`, `DACCVIOL`, `MUNSTKERR`, `MSTKERR`, `MLSPERR`,
     `MMARVALID`);
   - Bus (`IBUSERR`, `PRECISERR`, `IMPRECISERR`, `UNSTKERR`, `STKERR`,
     `LSPERR`, `BFARVALID`);
   - Usage (`UNDEFINSTR`, `INVSTATE`, `INVPC`, `NOCP`, `STKOF`, `UNALIGNED`,
     `DIVBYZERO`);
   - `HFSR FORCED` escalation, `VECTTBL` and `DEBUGEVT` observations.
   Precise/imprecise bus faults and stack push/pop faults are labelled
   separately; an imprecise fault leaves the address unknown rather than
   guessing it.
3. **Fault-address correlation**: when `MMARVALID`/`BFARVALID` accompanies the
   valid `MMFAR`/`BFAR` value, the address is mapped against declared `REGION`
   records (`FAULT_ADDRESS_REGION_MAPPED`) or reported outside the declared map
   (`FAULT_ADDRESS_OUTSIDE_DECLARED_MAP`). Access direction and offending
   instruction are explicitly not inferred.
4. **xPSR Thumb bit**: a clear bit 24 corroborates a UsageFault `INVSTATE`
   candidate (`XPSR_THUMB_BIT_CLEARED`); it does not locate the opcode.
5. **Frame stack accounting**: a frame SP matched to a supplied thread stack
   reports free bytes; when the free space is smaller than the frame footprint
   (about 32 bytes basic, 104 bytes extended), `POSSIBLE_EXCEPTION_STACK_OVERFLOW`
   records a candidate only. An SP outside all declared stacks yields
   `FRAME_SP_OUTSIDE_DECLARED_STACK`.
6. **PC symbolication**: a supplied `SYMB` table (in-file or `--symbols` file)
   resolves the stacked PC to `function + offset` (`PC_FUNCTION_RESOLVED`).
   Without a table, only region mapping from W12 is used.

## Findings and proof boundaries

| Code | Conservative meaning |
| --- | --- |
| `EXC_RETURN_MALFORMED` | Stacked LR violates the EXC_RETURN bit contract; possible misread/corruption. |
| `FRAME_FORMAT_MISMATCH` | Declared frame format disagrees with the EXC_RETURN FPU bit. |
| `MEMMANAGE_FAULT_FLAG` / `BUS_FAULT_*` / `USAGE_FAULT_FLAG` | A recorded status flag; not a root cause. |
| `HFSR_FORCED_ESCALATION` | A configurable fault escalated to HardFault; read CFSR for the origin. |
| `FAULT_ADDRESS_REGION_MAPPED` / `..._OUTSIDE_DECLARED_MAP` | Fault-status address vs declared memory map only. |
| `XPSR_THUMB_BIT_CLEARED` | Supports `INVSTATE`; opcode not located. |
| `POSSIBLE_EXCEPTION_STACK_OVERFLOW` | Frame footprint exceeds free recorded stack; overflow not proved. |
| `FRAME_SP_OUTSIDE_DECLARED_STACK` | SP not covered by declared stacks; metadata may be incomplete. |
| `PC_FUNCTION_RESOLVED` | PC matches supplied table; ELF/firmware identity unattested. |

Non-claims (unchanged W12 boundaries): no target halt or memory read; no stack
unwinding or return-chain reconstruction; no peripheral-register interpretation;
no fault provenance beyond supplied lines; no claim that a synthetic fixture
qualified hardware; RISC-V and other architectures are explicitly outside the
decoder.

## Usage

```sh
# Structured summary on stdout; JSON decode report on demand.
./build/rtos-observatory --fault-inspect examples/hardfault-busfault.tsv \
    --output build/hardfault-bus-report.json
./build/rtos-observatory --fault-inspect examples/hardfault-usagefault.tsv \
    --symbols path/to/symbols.tsv --output build/hardfault-usage-report.json
```

Output schema `rtos-observatory-hardfault/v1` carries per-frame summaries
(mode, stack, flags, fault address, owning thread, free bytes, PC resolution),
W28 findings, merged W12 structural findings, and warnings.

## Tests and qualification

- `observatory_hardfault_tests` (`tests/test_hardfault_decode.cpp`): known-answer
  precise bus fault with PC symbolication; UsageFault set on Armv8-M with clear
  Thumb bit and tight stack; unsupported architecture; missing frame; malformed
  EXC_RETURN; imprecise bus fault (address withheld); FPU/basic format mismatch;
  flat symbol table validation and rejection; failed CFSR read; JSON smoke.
- `observatory_hardfault_bus_fixture` and `..._usage_fixture`: CLI runs over the
  two checked-in synthetic fixtures.
- Local evidence: Linux GCC C++20 build, 35/35 CTest cases green on this branch.
  Windows/Release, sanitizers, clang-tidy, desktop panel and physical target
  qualification remain open follow-ups.

## Follow-ups (not in this branch)

1. Dear ImGui Fault panel grouping of W28 findings beside the W12 browser.
2. Extended-frame FPU word records (S0–S15/FPSCR) rather than format-only flags.
3. W13 ELF/DWARF identity-gated PC resolution with build-id verification.
4. Armv8-M secure-state and MSPLIM/PSPLIM register-specific observations.
5. RISC-V `mcause`/`mtval` decoder as a separate architecture module.

Reference: Armv7-M/Armv8-M exception model and `EXC_RETURN` documentation; see
[W12 references](W12_FAULT_SNAPSHOT.md). This document cites register semantics,
not hardware proof.
