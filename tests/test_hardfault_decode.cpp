// SPDX-License-Identifier: Apache-2.0
#include "observatory/hardfault.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace observatory;

static std::string header(const std::string& arch = "cortex-m4") {
    return "RTOS-OBSERVATORY-FAULT\t1\t" + arch + "\t32\tfw-synthetic\tdemo\tunverified\n";
}

static FaultSnapshot parse(const std::string& raw) {
    std::istringstream in(raw);
    return parse_fault_snapshot(in);
}

static HardFaultDecode decode(const std::string& raw,
                              const std::vector<SymbolEntry>& extra = {}) {
    return analyze_hardfault(parse(raw), extra);
}

static bool reject(const std::string& raw) {
    try { (void)parse(raw); } catch (const Error&) { return true; }
    return false;
}

static bool has(const HardFaultDecode& d, const std::string& code) {
    return std::any_of(d.findings.begin(), d.findings.end(),
                       [&](const HardFaultFinding& f) { return f.code == code; });
}

static const HardFaultFinding* find(const HardFaultDecode& d, const std::string& code) {
    for (const auto& f : d.findings)
        if (f.code == code) return &f;
    return nullptr;
}

static bool has_flag(const HardFaultFrameSummary& f, const std::string& flag) {
    return std::find(f.active_flags.begin(), f.active_flags.end(), flag) !=
           f.active_flags.end();
}

static std::string bus_fixture() {
    return header("cortex-m4") +
           "REGION\tflash\tflash\t0x08000000\t0x20000\trx\n"
           "REGION\tram\tram\t0x20002000\t0x8000\trw\n"
           "THREAD\tctrl\t0x20000020\t0x80\t0x20000800\t0x400\t0x20000bf0\t0x120\n"
           "THREAD\tsensor_task\t0x20000200\t0x80\t0x20002000\t0x800\t0x20002060\t0x500\n"
           "REG\tcpu0\tCFSR\t0x00008200\tok\n"
           "REG\tcpu0\tBFAR\t0x20002340\tok\n"
           "REG\tcpu0\tHFSR\t0x40000000\tok\n"
           "REG\tcpu0\tMMFAR\t-\tfailed\n"
           "REG\tcpu0\tPC\t0x08001244\tok\n"
           "FAULT\tHardFault\tcpu0\t0x08001244\n"
           "FRAME\tcpu0\t0x20002060\tbasic\t0x00000001\t0x00000002\t0x00000003\t"
           "0x00000004\t0x00000005\t0xFFFFFFFD\t0x08001244\t0x61000203\n"
           "SYMB\t0x08001200\t0x80\tsensor_sample_loop\n";
}

static std::string usage_fixture() {
    return header("cortex-m33") +
           "REGION\tflash\tflash\t0x08000000\t0x20000\trx\n"
           "REGION\tram\tram\t0x20002000\t0x8000\trw\n"
           "THREAD\tcomms\t0x20000200\t0x80\t0x20002000\t0x60\t0x20002050\t0x50\n"
           "REG\tcpu0\tCFSR\t0x00130000\tok\n"
           "REG\tcpu0\tHFSR\t0x40000000\tok\n"
           "REG\tcpu0\tPC\t0x08001000\tok\n"
           "FAULT\tHardFault\tcpu0\t0x08001000\n"
           "FRAME\tcpu0\t0x20002050\tbasic\t0x00000000\t0x20000010\t0x00000003\t"
           "0x00000004\t0x00000005\t0xFFFFFFF9\t0x08001000\t0x60000002\n";
}

int main() {
    // A: precise bus fault, Thread/PSP basic frame, PC symbolicated to supplied table.
    {
        auto d = decode(bus_fixture());
        assert(d.supported_arch);
        assert(d.frames.size() == 1);
        const auto& f = d.frames[0];
        assert(f.exc_return_valid);
        assert(f.mode == "Thread" && f.stack == "PSP");
        assert(!f.fpu_extended_frame);
        assert(f.pc_resolved && f.pc_name == "sensor_sample_loop" && f.pc_offset == 0x44);
        assert(f.fault_address && *f.fault_address == 0x20002340);
        assert(f.fault_region == "ram");
        assert(f.owning_thread == "sensor_task");
        assert(has_flag(f, "PRECISERR") && has_flag(f, "BFARVALID"));
        assert(has(d, "BUS_FAULT_PRECISE"));
        assert(has(d, "HFSR_FORCED_ESCALATION"));
        assert(has(d, "FAULT_ADDRESS_REGION_MAPPED"));
        assert(has(d, "PC_FUNCTION_RESOLVED"));
        const auto* pc = find(d, "PC_FUNCTION_RESOLVED");
        assert(pc && pc->address && *pc->address == 0x08001244);
    }

    // B: UsageFault set (incl. Armv8-M STKOF), clear Thumb bit, tight stack.
    {
        auto d = decode(usage_fixture());
        assert(d.supported_arch);
        const auto& f = d.frames[0];
        assert(f.mode == "Thread" && f.stack == "MSP");
        assert(f.thumb_bit.has_value() && !*f.thumb_bit);
        std::size_t usage = 0;
        bool saw_stkof = false;
        for (const auto& finding : d.findings)
            if (finding.code == "USAGE_FAULT_FLAG") {
                ++usage;
                if (finding.explanation.find("STKOF") != std::string::npos) saw_stkof = true;
            }
        assert(usage >= 3);
        assert(saw_stkof);
        assert(has(d, "XPSR_THUMB_BIT_CLEARED"));
        assert(has(d, "POSSIBLE_EXCEPTION_STACK_OVERFLOW"));
        assert(f.stack_free_bytes && *f.stack_free_bytes < 32);
    }

    // C: non-Cortex-M architecture: decode skipped, warning only, no invented cause.
    {
        auto d = decode(header("riscv32") + "REG\tcpu0\tCFSR\t0x200\tok\n");
        assert(!d.supported_arch);
        assert(d.findings.empty());
        assert(std::any_of(d.warnings.begin(), d.warnings.end(),
                           [](const std::string& w) { return w.find("not a recognized") != std::string::npos; }));
    }

    // D: no supplied frame: explicit unknown warning.
    {
        auto d = decode(header() + "REG\tcpu0\tCFSR\t0x200\tok\n");
        assert(std::any_of(d.warnings.begin(), d.warnings.end(),
                           [](const std::string& w) { return w.find("No supplied exception frame") != std::string::npos; }));
    }

    // E: malformed EXC_RETURN value.
    {
        const std::string raw = header() +
            "FRAME\tcpu0\t-\tbasic\t-\t-\t-\t-\t-\t0x12345678\t-\t-\n";
        auto d = decode(raw);
        assert(has(d, "EXC_RETURN_MALFORMED"));
        assert(!d.frames[0].exc_return_valid);
    }

    // F: parser rejects malformed W28 records.
    {
        const std::string base = header();
        assert(reject(base + "FRAME\tcpu0\t0x100\tbasic\n"));
        assert(reject(base + "FRAME\tcpu0\t0x100\twide\t-\t-\t-\t-\t-\t-\t-\t-\n"));
        assert(reject(base + "SYMB\t0x08001000\t0\tfn\n"));
        assert(reject(base + "SYMB\t0x08001000\t0x80\tfn\nSYMB\t0x08001000\t0x80\tfn\n"));
        assert(reject(base +
                      "FRAME\tcpu0\t0x100\tbasic\t-\t-\t-\t-\t-\t0xFFFFFFFD\t-\t-\n"
                      "FRAME\tcpu0\t0x104\tbasic\t-\t-\t-\t-\t-\t0xFFFFFFFD\t-\t-\n"));
        assert(reject(base + "FRAME\tcpu0\t0x100\tbasic\t0x100000000\t-\t-\t-\t-\t0xFFFFFFFD\t-\t-\n"));
        assert(reject(base + "SYMB\tnothex\t0x80\tfn\n"));
    }

    // G: imprecise bus fault leaves fault address unknown.
    {
        const std::string raw = header("cortex-m4") +
            "REG\tcpu0\tCFSR\t0x00000400\tok\n" +
            "REG\tcpu0\tBFAR\t-\tfailed\n" +
            "FRAME\tcpu0\t0x20002060\tbasic\t-\t-\t-\t-\t-\t0xFFFFFFFD\t0x08001000\t0x61000003\n";
        auto d = decode(raw);
        assert(has(d, "BUS_FAULT_IMPRECISE"));
        assert(!d.frames[0].fault_address);
    }

    // H: declared FPU frame conflicts with EXC_RETURN basic indicator.
    {
        const std::string raw = header("cortex-m4") +
            "FRAME\tcpu0\t0x20002060\tfpu\t-\t-\t-\t-\t-\t0xFFFFFFFD\t0x08001000\t0x61000003\n";
        auto d = decode(raw);
        assert(has(d, "FRAME_FORMAT_MISMATCH"));
    }

    // I: flat symbol table parser, valid rows and rejection of a bad row.
    {
        std::istringstream sym(
            "0x08001200\t0x80\tapp_task\n# a comment\n0x08002000\t0x100\tlib_init\n");
        auto table = parse_symbol_table(sym);
        assert(table.size() == 2);
        assert(table[0].name == "app_task" && table[0].address == 0x08001200);
        std::istringstream bad("0x08001200\tnot-size\tapp\n");
        bool thrown = false;
        try { (void)parse_symbol_table(bad); } catch (const Error&) { thrown = true; }
        assert(thrown);
    }

    // J: external symbol table resolves PC when no in-snapshot SYMB row exists.
    {
        const std::string raw = header() +
            "FRAME\tcpu0\t-\tbasic\t-\t-\t-\t-\t-\t0xFFFFFFFD\t0x08002010\t0x61000003\n";
        std::vector<SymbolEntry> extra{{"lib_init", 0x08002000, 0x100, 1}};
        auto d = decode(raw, extra);
        assert(d.frames[0].pc_resolved);
        assert(d.frames[0].pc_name == "lib_init" && d.frames[0].pc_offset == 0x10);
        assert(has(d, "PC_FUNCTION_RESOLVED"));
    }

    // K: failed CFSR read surfaces as an explicit unknown-value warning.
    {
        const std::string raw = header() + "REG\tcpu0\tCFSR\t-\tfailed\n";
        auto d = decode(raw);
        assert(std::any_of(d.warnings.begin(), d.warnings.end(),
                           [](const std::string& w) { return w.find("CFSR") != std::string::npos; }));
    }

    // L: JSON serialization smoke, including merged W12 structural findings.
    {
        const auto snap = parse(bus_fixture());
        const auto view = analyze_fault_snapshot(snap);
        const auto d = analyze_hardfault(snap);
        const auto json = hardfault_decode_json(d, &view);
        assert(json.find("rtos-observatory-hardfault/v1") != std::string::npos);
        assert(json.find("BUS_FAULT_PRECISE") != std::string::npos);
    }

    std::cout << "W28 Cortex-M HardFault decode known-answer and malformed-input tests passed\n";
}
