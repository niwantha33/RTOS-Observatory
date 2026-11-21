<!-- SPDX-License-Identifier: Apache-2.0 -->
# ELF-01a — native offline ELF Intelligence

Entry points:
- Desktop **Analysis > Source / symbols > ELF Intelligence**: browse local firmware ELF, inspect architecture/hash/build-id, browse actual function symbols and look up a numeric PC.
- CLI: \`rtos-observatory --elf-inspect firmware.elf\`. Output is a bounded human-readable inspection summary.
- Existing W13 **Advanced: legacy sidecar source graph** is preserved unchanged.

Native ELF parser supports bounded little-endian ELF32/ELF64 for ARM, AArch64, RISC-V, Xtensa, x86, x86_64, MIPS and PowerPC. It extracts STT_FUNC / GNU IFUNC symbols from actual .symtab/.dynsym section entries, GNU build-id notes and the SHA-256 of exactly the bytes read, with section/offset/count/name bounds. ARM Thumb odd function addresses are normalized. Ambiguous or zero-size PC intervals do not resolve. ET_REL and ET_DYN runtime PC lookups are deliberately refused without linking/loading evidence. No library download, target access or external subprocess is involved.

**Not included and never inferred from ELF alone:**
- No native DWARF line tables, source paths, local variables, unwind tables, inline call sites, demangling, stack high-water, dynamic call graph, CPU load or execution durations.
- No ELF-to-trace identity attestation. Capture firmware labels and ELF build-id **must not** be treated as authenticated matches automatically. A PC from a trace must be a *verified code address*, not a thread pointer or mutex handle.
- No real-target measurement or timing qualification. Trace gaps, unknown clock scaling and reboot epochs remain unqualified unless the source adapter provides the corresponding evidence.
- GNU build-id is a useful identity hint but is not a cryptographic signature.

The native ELF view and W13 sidecar graph are **separate evidence sources**. A sidecar's claimed functions and call relationships are never silently recast as data extracted from the ELF.

## Test/quality gates

CTest \`observatory_elf_native_symbols\` builds synthetic ELF32 ARM/Thumb and ELF64 images and exercises valid functions, GNU build-id, debug-section detection, overlapping ranges, unsupported machine/endianness, malformed sections and symbol limits. No real/customer ELF is committed to this public repository.

Acceptance before merge: Windows + Linux CMake build, CTest, GUI smoke, independent review. Native source-line/DWARF parsing and trace link proof are separate gated phases in [ELF-01 tracker](https://github.com/niwantha33/RTOS-Observatory/issues/55). Do not mark those phases complete based on this package.
