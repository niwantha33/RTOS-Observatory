// SPDX-License-Identifier: Apache-2.0
#pragma once

// W28 — offline Cortex-M HardFault evidence decoder.
//
// Boundary: this module interprets records that the external source supplied
// (a snapshot parser already consumed), especially an explicitly stacked
// exception frame, CFSR/HFSR and fault-address status registers, a declared
// memory map and an optional function symbol table. It never reads a target,
// disassembles memory, reconstructs a frame that was not supplied, or proves
// a root cause. "Possible" and "candidate" labels denote observations, not
// conclusions; a failed read remains unknown rather than zero.

#include "observatory/fault_evidence.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <charconv>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace observatory {

struct HardFaultFinding {
    std::string code;
    std::string subject;
    std::string explanation;
    std::vector<std::size_t> source_lines;
    std::optional<std::uint64_t> address;
};

struct HardFaultFrameSummary {
    std::string context;
    std::string format; // basic | fpu
    bool supported_arch{false};
    std::optional<std::uint64_t> sp;
    std::optional<std::uint64_t> exc_return;
    bool exc_return_valid{false};
    std::string mode;    // Thread | Handler | unknown
    std::string stack;  // MSP | PSP | unknown
    bool fpu_extended_frame{false};
    std::optional<std::uint64_t> pc;
    bool pc_resolved{false};
    std::string pc_name;
    std::uint64_t pc_offset{};
    std::optional<bool> thumb_bit;
    std::string dominant_class;
    std::vector<std::string> active_flags;
    std::optional<std::uint64_t> fault_address;
    std::string fault_region;
    std::string owning_thread;
    std::optional<std::uint64_t> stack_free_bytes;
};

struct HardFaultDecode {
    std::string firmware_label;
    std::string capture_id;
    std::string architecture;
    bool supported_arch{false};
    std::vector<HardFaultFrameSummary> frames;
    std::vector<HardFaultFinding> findings;
    std::vector<std::string> warnings;
};

// Independent flat symbol table: address<TAB>size<TAB>name, '#' comments.
struct SymbolTableLimits {
    std::size_t max_bytes{256 * 1024};
    std::size_t max_line_bytes{1024};
    std::size_t max_symbols{4096};
};

namespace hf_detail {

struct FlagDef {
    unsigned bit;
    const char* name;
    const char* group; // MMU | BUS | USAGE
    const char* meaning;
};

inline std::vector<FlagDef> cfsr_flags() {
    static const std::vector<FlagDef> table = {
        {0,  "IACCVIOL", "MMU",    "Instruction access to a region the memory model forbids"},
        {1,  "DACCVIOL", "MMU",    "Data access to a region the memory model forbids"},
        {3,  "MUNSTKERR","MMU",    "MemManage fault while popping the exception frame on return"},
        {4,  "MSTKERR",  "MMU",    "MemManage fault while pushing the exception frame on entry"},
        {5,  "MLSPERR",  "MMU",    "MemManage fault during lazy floating-point stacking"},
        {7,  "MMARVALID","MMU",    "MMFAR holds a valid MemManage fault address"},
        {8,  "IBUSERR",  "BUS",    "Instruction prefetch/bus error on an attempted fetch"},
        {9,  "PRECISERR","BUS",    "Precise data bus fault; BFAR is valid when BFARVALID is set"},
        {10, "IMPRECISERR","BUS",  "Imprecise (asynchronous) bus fault; faulting address not reliable"},
        {11, "UNSTKERR", "BUS",    "Bus fault while popping the exception frame on return"},
        {12, "STKERR",   "BUS",    "Bus fault while pushing the exception frame on entry"},
        {13, "LSPERR",   "BUS",    "Bus fault during lazy floating-point stacking"},
        {15, "BFARVALID","BUS",    "BFAR holds a valid bus fault address"},
        {16, "UNDEFINSTR","USAGE", "Undefined instruction opcode was attempted to execute"},
        {17, "INVSTATE", "USAGE",  "Invalid EPSR state; the PC Thumb (T) bit may be clear"},
        {18, "INVPC",    "USAGE",  "Invalid EXC_RETURN/PC load; corrupted stack or return path possible"},
        {19, "NOCP",     "USAGE",  "Coprocessor/FP instruction executed while the FPU/CP is disabled"},
        {20, "STKOF",    "USAGE",  "Armv8-M stack-limit (MSPLIM/PSPLIM) overflow candidate"},
        {24, "UNALIGNED","USAGE",  "Unaligned access performed while alignment checking is enabled"},
        {25, "DIVBYZERO","USAGE",  "Integer divide by zero with DIV_0_TRP trapping enabled"},
    };
    return table;
}

inline const FaultRegister* find_reg(const FaultSnapshot& s,
                                      std::string_view name,
                                      std::string_view context) {
    for (const auto& r : s.registers)
        if (r.name == name && r.context == std::string(context)) return &r;
    for (const auto& r : s.registers)
        if (r.name == name) return &r;
    return nullptr;
}

inline const FaultRegion* region_for(const FaultSnapshot& s,
                                     std::uint64_t address,
                                     std::uint64_t size = 1) {
    for (const auto& r : s.regions)
        if (fault_detail::contains(r, address, size)) return &r;
    return nullptr;
}

inline bool is_cortex_m_32(const FaultSnapshot& s) {
    if (s.address_bits != 32) return false;
    static const char* known[] = {
        "cortex-m3", "cortex-m4", "cortex-m7", "cortex-m23",
        "cortex-m33", "cortex-m35p", "cortex-m55", "cortex-m85"};
    for (const auto* k : known)
        if (s.architecture == k) return true;
    return false;
}

struct ExcReturn {
    bool valid{false};
    bool thread{false};
    bool psp{false};
    bool fpu_extended{false};
};

// EXC_RETURN encoding observed in standard Cortex-M return constants
// (0xFFFFFFF1 handler/MSP basic, 0xFFFFFFF9 thread/MSP basic,
// 0xFFFFFFFD thread/PSP basic, 0xFFFFFFE1/E9/ED FPU-extended variants):
// bit3 = Mode (1 Thread / 0 Handler), bit2 = Stack (1 PSP / 0 MSP),
// bit4 = 0 for an extended floating-point frame, 1 for the basic integer frame.
inline ExcReturn decode_exc_return(std::uint64_t v) {
    ExcReturn e;
    // Reserved high bits must be set; bit0 is always 1.
    if ((v & UINT64_C(0xffffffe0)) != UINT64_C(0xffffffe0) || (v & 1U) == 0)
        return e;
    e.valid = true;
    e.thread = (v & UINT64_C(0x8)) != 0;
    e.psp = (v & UINT64_C(0x4)) != 0;
    e.fpu_extended = (v & UINT64_C(0x10)) == 0;
    return e;
}

inline std::string json_escape(const std::string& raw) {
    std::string out;
    out.push_back('"');
    for (const unsigned char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

inline std::string hex64(std::uint64_t v) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

inline std::optional<std::uint64_t> parse_hex_cell(const std::string& raw) {
    const std::string_view s(raw);
    const auto digits = (s.starts_with("0x") || s.starts_with("0X"))
                            ? s.substr(2)
                            : s;
    if (digits.empty()) return std::nullopt;
    std::uint64_t n = 0;
    const auto* begin = digits.data();
    const auto* end = digits.data() + digits.size();
    auto [p, ec] = std::from_chars(begin, end, n, 16);
    if (ec != std::errc{} || p != end) return std::nullopt;
    return n;
}

} // namespace hf_detail

// Flat symbol table reader; each accepted row is address, size, name.
inline std::vector<SymbolEntry> parse_symbol_table(std::istream& input,
                                                   SymbolTableLimits limits = {}) {
    if (!limits.max_bytes || !limits.max_line_bytes || !limits.max_symbols)
        throw Error("W28 symbol table limits must be positive");
    std::vector<SymbolEntry> out;
    std::string line;
    std::size_t total = 0, line_no = 0;
    while (std::getline(input, line)) {
        ++line_no;
        total += line.size() + 1;
        if (total > limits.max_bytes) throw Error("W28 symbol table byte limit exceeded");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        if (line.size() > limits.max_line_bytes) throw Error("W28 symbol line too long");
        std::vector<std::string> cells;
        std::size_t start = 0;
        while (true) {
            const auto end = line.find('\t', start);
            cells.push_back(line.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (cells.size() != 3) throw Error("W28 symbol row must be address, size, name");
        const auto address = hf_detail::parse_hex_cell(cells[0]);
        const auto size = hf_detail::parse_hex_cell(cells[1]);
        if (!address || !size || *size == 0)
            throw Error("W28 symbol row has invalid address or size");
        if (cells[2].empty() || cells[2].size() > 64)
            throw Error("W28 symbol name length invalid");
        for (const unsigned char c : cells[2])
            if (c < 33 || c > 126 || c == '"' || c == '\\')
                throw Error("W28 symbol name has invalid character");
        if (out.size() >= limits.max_symbols) throw Error("W28 symbol count limit reached");
        out.push_back({cells[2], *address, *size, line_no});
    }
    std::sort(out.begin(), out.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) { return a.address < b.address; });
    return out;
}

inline HardFaultDecode analyze_hardfault(const FaultSnapshot& snap,
                                          const std::vector<SymbolEntry>& extra_symbols = {}) {
    HardFaultDecode out;
    out.firmware_label = snap.firmware_label;
    out.capture_id = snap.capture_id;
    out.architecture = snap.architecture;
    out.supported_arch = hf_detail::is_cortex_m_32(snap);

    std::vector<SymbolEntry> symbols = snap.symbols;
    symbols.insert(symbols.end(), extra_symbols.begin(), extra_symbols.end());
    std::sort(symbols.begin(), symbols.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) { return a.address < b.address; });

    auto add = [&](const std::string& code, std::string subject, std::string explanation,
                   std::vector<std::size_t> lines, std::optional<std::uint64_t> address = {}) {
        out.findings.push_back({code, std::move(subject), std::move(explanation),
                                std::move(lines), address});
    };

    if (!out.supported_arch) {
        out.warnings.push_back(
            "Architecture label '" + snap.architecture +
            "' is not a recognized 32-bit Cortex-M; exception register and frame "
            "decoding skipped. Supplied records are preserved without interpretation.");
    }
    if (snap.frames.empty()) {
        out.warnings.push_back(
            "No supplied exception frame: EXC_RETURN, stacked PC and xPSR cannot be "
            "interpreted; only register-level status flags are available.");
    }

    // Warn explicitly when a status register record exists but its read failed.
    for (const char* name : {"CFSR", "HFSR", "MMFAR", "BFAR"}) {
        const auto* reg = hf_detail::find_reg(snap, name, "");
        if (reg && !reg->value)
            out.warnings.push_back(std::string(name) +
                                    " read failed on line " + std::to_string(reg->line) +
                                    ": its fault status and address remain unknown.");
    }

    for (const auto& frame : snap.frames) {
        HardFaultFrameSummary fs;
        fs.context = frame.context;
        fs.format = frame.format;
        fs.supported_arch = out.supported_arch;
        fs.sp = frame.sp;
        fs.pc = frame.pc;

        if (!out.supported_arch) {
            out.frames.push_back(std::move(fs));
            continue;
        }

        // --- EXC_RETURN (stacked LR) ---
        const auto* lr_reg = frame.lr ? nullptr
                                      : hf_detail::find_reg(snap, "EXC_RETURN", frame.context);
        const std::optional<std::uint64_t> er =
            frame.lr ? frame.lr
                     : (lr_reg && lr_reg->value ? lr_reg->value : std::nullopt);
        if (!er) {
            out.warnings.push_back("Frame '" + frame.context +
                                   "': stacked LR/EXC_RETURN missing; return mode and "
                                   "stack pointer selection are unknown.");
        } else {
            fs.exc_return = er;
            const auto ex = hf_detail::decode_exc_return(*er);
            fs.exc_return_valid = ex.valid;
            fs.mode = ex.valid ? (ex.thread ? "Thread" : "Handler") : "unknown";
            fs.stack = ex.valid ? (ex.psp ? "PSP" : "MSP") : "unknown";
            fs.fpu_extended_frame = ex.valid && ex.fpu_extended;
            if (!ex.valid) {
                add("EXC_RETURN_MALFORMED", frame.context,
                    "Stacked LR " + hf_detail::hex64(*er) +
                        " is not a valid EXC_RETURN bit pattern; the frame may be "
                        "misread or corrupted. This is a register-value observation, "
                        "not a root-cause proof.",
                    {frame.line}, er);
            } else {
                const bool record_fpu = (frame.format == "fpu");
                if (record_fpu != ex.fpu_extended)
                    add("FRAME_FORMAT_MISMATCH", frame.context,
                        "Declared frame format '" + frame.format +
                            "' disagrees with EXC_RETURN FPU-context bit "
                            "(extended=" + (ex.fpu_extended ? "yes" : "no") +
                            "); verify capture metadata and frame word count.",
                        {frame.line}, er);
            }
        }

        // --- CFSR / HFSR classification ---
        const auto* cfsr = hf_detail::find_reg(snap, "CFSR", frame.context);
        const auto* hfsr = hf_detail::find_reg(snap, "HFSR", frame.context);
        const auto* mmfar = hf_detail::find_reg(snap, "MMFAR", frame.context);
        const auto* bfar = hf_detail::find_reg(snap, "BFAR", frame.context);

        std::vector<std::string> classes;
        auto note_class = [&](const char* group) {
            const char* friendly = std::string(group) == "MMU" ? "MEMMANAGE_FAULT"
                                   : std::string(group) == "BUS" ? "BUS_FAULT"
                                                                 : "USAGE_FAULT";
            if (std::find(classes.begin(), classes.end(), friendly) == classes.end())
                classes.emplace_back(friendly);
        };
        std::optional<std::uint64_t> fault_address;

        if (cfsr && cfsr->value) {
            const std::uint64_t v = *cfsr->value;
            if (v > UINT64_C(0xffffffff)) {
                add("FAULT_STATUS_WIDTH_INVALID", frame.context + " / CFSR",
                    "Cortex-M CFSR is a 32-bit register; the supplied value exceeds 32 bits.",
                    {cfsr->line}, cfsr->value);
            } else {
                for (const auto& flag : hf_detail::cfsr_flags()) {
                    if ((v & (UINT64_C(1) << flag.bit)) == 0) continue;
                    fs.active_flags.push_back(flag.name);
                    note_class(flag.group);
                    const std::string meaning = flag.meaning;
                    if (std::string(flag.group) == "MMU") {
                        add("MEMMANAGE_FAULT_FLAG", frame.context,
                            "CFSR MemManage flag " + std::string(flag.name) + ": " + meaning +
                                ". A status flag alone does not identify the offending "
                                "instruction or prove the access type.",
                            {cfsr->line, frame.line}, cfsr->value);
                    } else if (std::string(flag.group) == "BUS") {
                        const char* code = "BUS_FAULT_FLAG";
                        if (std::string(flag.name) == "PRECISERR") code = "BUS_FAULT_PRECISE";
                        else if (std::string(flag.name) == "IMPRECISERR") code = "BUS_FAULT_IMPRECISE";
                        else if (std::string(flag.name) == "STKERR") code = "BUS_FAULT_STACK_PUSH";
                        else if (std::string(flag.name) == "UNSTKERR") code = "BUS_FAULT_STACK_POP";
                        else if (std::string(flag.name) == "IBUSERR") code = "BUS_FAULT_INSTRUCTION";
                        add(code, frame.context,
                            "CFSR BusFault flag " + std::string(flag.name) + ": " + meaning,
                            {cfsr->line, frame.line}, cfsr->value);
                    } else {
                        add("USAGE_FAULT_FLAG", frame.context,
                            "CFSR UsageFault flag " + std::string(flag.name) + ": " + meaning +
                                ". Treat as a candidate cause until the instruction and "
                                "input values are examined.",
                            {cfsr->line, frame.line}, cfsr->value);
                    }
                }
                if ((v & (UINT64_C(1) << 7)) && mmfar && mmfar->value)
                    fault_address = mmfar->value;
                if ((v & (UINT64_C(1) << 15)) && bfar && bfar->value)
                    fault_address = bfar->value;
            }
        }

        if (hfsr && hfsr->value) {
            const std::uint64_t v = *hfsr->value;
            if (v & (UINT64_C(1) << 30))
                add("HFSR_FORCED_ESCALATION", frame.context,
                    "HFSR FORCED set: a configurable MemManage/Bus/Usage fault was "
                        "escalated to HardFault. Read CFSR for the originating cause; "
                        "this bit is not itself a root cause.",
                    {hfsr->line, frame.line}, hfsr->value);
            if (v & (UINT64_C(1) << 1))
                add("HFSR_VECTTBL", frame.context,
                    "HFSR VECTTBL set: a vector table fetch/usage error was recorded; "
                        "check vector table alignment and the faulting exception number.",
                    {hfsr->line, frame.line}, hfsr->value);
            if (v & (UINT64_C(1) << 31))
                add("HFSR_DEBUG_EVENT", frame.context,
                    "HFSR DEBUGEVT set: debug-event escalation recorded; verify the "
                        "debug configuration before interpreting as a firmware fault.",
                    {hfsr->line, frame.line}, hfsr->value);
        }

        if (fault_address) {
            const auto* region = hf_detail::region_for(snap, *fault_address);
            if (region) {
                fs.fault_region = region->name;
                add("FAULT_ADDRESS_REGION_MAPPED", frame.context,
                    "Fault-status address " + hf_detail::hex64(*fault_address) +
                        " falls in declared region '" + region->name + "' (" + region->kind +
                        ", access " + region->access + "). Access direction and the "
                        "offending instruction are not proven by the snapshot.",
                    {frame.line}, fault_address);
            } else {
                add("FAULT_ADDRESS_OUTSIDE_DECLARED_MAP", frame.context,
                    "Fault-status address " + hf_detail::hex64(*fault_address) +
                        " is outside every declared region; the memory map may be "
                        "incomplete rather than the value being corrupted.",
                    {frame.line}, fault_address);
            }
            fs.fault_address = fault_address;
        }

        fs.dominant_class = classes.empty() ? std::string() : classes.front();

        // --- xPSR Thumb bit ---
        if (frame.xpsr) {
            const bool thumb = (*frame.xpsr & (UINT64_C(1) << 24)) != 0;
            fs.thumb_bit = thumb;
            if (!thumb)
                add("XPSR_THUMB_BIT_CLEARED", frame.context,
                    "Stacked xPSR Thumb bit (bit 24) is clear on an ARM Cortex-M; "
                        "ARMv7-M/8-M execution requires Thumb state. This corroborates "
                        "a UsageFault INVSTATE candidate but does not locate the opcode.",
                    {frame.line}, frame.xpsr);
        }

        // --- Stack-frame accounting against supplied thread metadata ---
        if (frame.sp) {
            const FaultThread* owner = nullptr;
            for (const auto& t : snap.threads)
                if (t.observed_sp && *t.observed_sp == *frame.sp) { owner = &t; break; }
            if (!owner)
                for (const auto& t : snap.threads)
                    if (*frame.sp >= t.stack_address &&
                        *frame.sp - t.stack_address < t.stack_size) { owner = &t; break; }
            if (!owner) {
                add("FRAME_SP_OUTSIDE_DECLARED_STACK", frame.context,
                    "Stacked SP " + hf_detail::hex64(*frame.sp) +
                        " does not fall within any declared thread stack allocation; "
                        "stack metadata may be incomplete or the frame may be misplaced.",
                    {frame.line}, frame.sp);
            } else {
                fs.owning_thread = owner->name;
                if (frame.sp >= owner->stack_address &&
                    owner->stack_address + owner->stack_size >= *frame.sp) {
                    const std::uint64_t free_bytes =
                        (owner->stack_address + owner->stack_size) - *frame.sp;
                    fs.stack_free_bytes = free_bytes;
                    const std::uint64_t required = (frame.format == "fpu") ? 104U : 32U;
                    if (free_bytes < required)
                        add("POSSIBLE_EXCEPTION_STACK_OVERFLOW", owner->name,
                            "Stacked SP leaves " + std::to_string(free_bytes) +
                                " free bytes but the supplied '" + frame.format +
                                "' frame requires about " + std::to_string(required) +
                                " bytes; a stack-overflow candidate. Overflow is not "
                                "proven: SP, frame format or stack metadata may be off.",
                            {frame.line, owner->line}, frame.sp);
                }
            }
        }

        // --- PC symbolication against supplied table only ---
        if (frame.pc) {
            const SymbolEntry* match = nullptr;
            for (const auto& sym : symbols) {
                if (*frame.pc >= sym.address &&
                    *frame.pc - sym.address < sym.size) {
                    if (!match || sym.size < match->size) match = &sym;
                }
            }
            if (match) {
                fs.pc_resolved = true;
                fs.pc_name = match->name;
                fs.pc_offset = *frame.pc - match->address;
                add("PC_FUNCTION_RESOLVED", frame.context,
                    "Stacked PC " + hf_detail::hex64(*frame.pc) + " matches supplied "
                        "function '" + match->name + "' +0x" +
                        hf_detail::hex64(fs.pc_offset).substr(2) +
                        ". The symbol table and firmware ELF identity are not attested "
                        "by this offline decoder.",
                    {frame.line, match->line}, frame.pc);
            }
        }

        out.frames.push_back(std::move(fs));
    }

    if (out.findings.empty() && out.supported_arch && !snap.frames.empty())
        out.warnings.push_back("No conservative classification could be established "
                               "from the supplied records; inspect raw registers and "
                               "memory manually.");
    return out;
}

// Serialize the decode plus, when supplied, the W12 structural findings.
inline std::string hardfault_decode_json(const HardFaultDecode& d,
                                          const FaultView* structural = nullptr) {
    std::ostringstream s;
    s << "{\n  \"schema\": \"rtos-observatory-hardfault/v1\",\n"
      << "  \"firmware\": " << hf_detail::json_escape(d.firmware_label) << ",\n"
      << "  \"capture\": " << hf_detail::json_escape(d.capture_id) << ",\n"
      << "  \"architecture\": " << hf_detail::json_escape(d.architecture) << ",\n"
      << "  \"supported_arch\": " << (d.supported_arch ? "true" : "false") << ",\n"
      << "  \"frames\": [";
    for (std::size_t i = 0; i < d.frames.size(); ++i) {
        if (i) s << ',';
        const auto& f = d.frames[i];
        s << "\n    {\"context\": " << hf_detail::json_escape(f.context)
          << ", \"format\": " << hf_detail::json_escape(f.format)
          << ", \"mode\": " << hf_detail::json_escape(f.mode)
          << ", \"stack\": " << hf_detail::json_escape(f.stack)
          << ", \"fpu_extended_frame\": " << (f.fpu_extended_frame ? "true" : "false")
          << ", \"exc_return_valid\": " << (f.exc_return_valid ? "true" : "false");
        if (f.exc_return) s << ", \"exc_return\": " << hf_detail::json_escape(hf_detail::hex64(*f.exc_return));
        if (f.sp) s << ", \"sp\": " << hf_detail::json_escape(hf_detail::hex64(*f.sp));
        if (f.pc) s << ", \"pc\": " << hf_detail::json_escape(hf_detail::hex64(*f.pc));
        s << ", \"pc_resolved\": " << (f.pc_resolved ? "true" : "false")
          << ", \"pc_name\": " << hf_detail::json_escape(f.pc_name)
          << ", \"pc_offset\": " << f.pc_offset;
        if (f.thumb_bit) s << ", \"thumb_bit\": " << (*f.thumb_bit ? "true" : "false");
        s << ", \"dominant_class\": " << hf_detail::json_escape(f.dominant_class)
          << ", \"active_flags\": [";
        for (std::size_t j = 0; j < f.active_flags.size(); ++j) {
            if (j) s << ',';
            s << hf_detail::json_escape(f.active_flags[j]);
        }
        s << "], \"fault_address\": ";
        if (f.fault_address) s << hf_detail::json_escape(hf_detail::hex64(*f.fault_address));
        else s << "null";
        s << ", \"fault_region\": " << hf_detail::json_escape(f.fault_region)
          << ", \"owning_thread\": " << hf_detail::json_escape(f.owning_thread);
        s << ", \"stack_free_bytes\": ";
        if (f.stack_free_bytes) s << *f.stack_free_bytes;
        else s << "null";
        s << '}';
    }
    s << (!d.frames.empty() ? "\n  ],\n" : "],\n");

    s << "  \"findings\": [";
    for (std::size_t i = 0; i < d.findings.size(); ++i) {
        if (i) s << ',';
        const auto& f = d.findings[i];
        s << "\n    {\"code\": " << hf_detail::json_escape(f.code)
          << ", \"subject\": " << hf_detail::json_escape(f.subject)
          << ", \"explanation\": " << hf_detail::json_escape(f.explanation)
          << ", \"lines\": [";
        for (std::size_t j = 0; j < f.source_lines.size(); ++j) {
            if (j) s << ',';
            s << f.source_lines[j];
        }
        s << "], \"address\": ";
        if (f.address) s << hf_detail::json_escape(hf_detail::hex64(*f.address));
        else s << "null";
        s << '}';
    }
    s << (!d.findings.empty() ? "\n  ],\n" : "],\n");

    s << "  \"structural_findings\": [";
    if (structural) {
        bool first = true;
        for (const auto& f : structural->findings) {
            if (!first) s << ',';
            first = false;
            s << "\n    {\"code\": " << hf_detail::json_escape(f.code)
              << ", \"subject\": " << hf_detail::json_escape(f.subject)
              << ", \"explanation\": " << hf_detail::json_escape(f.explanation)
              << ", \"lines\": [";
            for (std::size_t j = 0; j < f.source_lines.size(); ++j) {
                if (j) s << ',';
                s << f.source_lines[j];
            }
            s << "], \"address\": ";
            if (f.address) s << hf_detail::json_escape(hf_detail::hex64(*f.address));
            else s << "null";
            s << '}';
        }
    }
    s << (!structural || structural->findings.empty() ? "],\n" : "\n  ],\n");

    bool first_warning = true;
    s << "  \"warnings\": [";
    for (const auto& w : d.warnings) {
        if (!first_warning) s << ',';
        first_warning = false;
        s << hf_detail::json_escape(w);
    }
    if (structural)
        for (const auto& w : structural->warnings) {
            if (!first_warning) s << ',';
            first_warning = false;
            s << hf_detail::json_escape(w);
        }
    s << "]\n}\n";
    return s.str();
}

} // namespace observatory
