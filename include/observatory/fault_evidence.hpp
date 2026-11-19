// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "observatory/engine.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace observatory {

// W12 is an OFFLINE, externally supplied snapshot reader. No probe attachment,
// target reads, TCB layout guessing, stack scans or unwinding is performed.
struct FaultLimits {
    std::size_t max_input_bytes{1024 * 1024};
    std::size_t max_line_bytes{4096};
    std::size_t max_regions{128};
    std::size_t max_threads{1024};
    std::size_t max_registers{2048};
    std::size_t max_mem_samples{256};
    std::size_t max_total_mem_bytes{64 * 1024};
    std::size_t max_findings{4096};
};
struct FaultRegion {
    std::string name;
    std::string kind; // ram, flash, peripheral, other
    std::string access; // r, rw, rx, rwx, none
    std::uint64_t address{};
    std::uint64_t size{};
    std::size_t line{};
};
struct FaultThread {
    std::string name;
    std::uint64_t tcb_address{};
    std::uint64_t tcb_size{};
    std::uint64_t stack_address{};
    std::uint64_t stack_size{};
    std::optional<std::uint64_t> observed_sp;
    std::optional<std::uint64_t> reported_stack_used_bytes;
    std::size_t line{};
};
struct FaultHeap {
    std::string name;
    std::uint64_t address{};
    std::uint64_t size{};
    std::optional<std::uint64_t> reported_used_bytes;
    std::optional<std::uint64_t> reported_peak_bytes;
    std::size_t line{};
};
struct FaultRegister {
    std::string context;
    std::string name;
    std::optional<std::uint64_t> value; // missing means explicit failed read
    std::size_t line{};
};
struct FaultMarker {
    std::string type;
    std::string context;
    std::optional<std::uint64_t> reported_address;
    std::size_t line{};
};
struct FaultMemory {
    std::string region;
    std::uint64_t address{};
    bool read_succeeded{false};
    std::vector<std::uint8_t> bytes;
    std::size_t line{};
};
struct FaultSnapshot {
    std::string architecture;
    unsigned address_bits{};
    std::string firmware_label;
    std::string capture_id;
    bool claims_coherent{false}; // self-declared, NOT independently verified
    std::vector<FaultRegion> regions;
    std::vector<FaultThread> threads;
    std::vector<FaultHeap> heaps;
    std::vector<FaultRegister> registers;
    std::vector<FaultMarker> markers;
    std::vector<FaultMemory> memory;
};
struct FaultFinding {
    std::string code;
    std::string subject;
    std::string explanation;
    std::vector<std::size_t> source_lines;
    std::optional<std::uint64_t> address;
};
struct FaultView {
    std::size_t examined_records{};
    bool claimed_coherent{false};
    bool independently_attested{false}; // always false in this offline schema
    bool findings_capped{false};
    std::vector<FaultFinding> findings;
    std::vector<std::string> warnings;
};

namespace fault_detail {
inline void token(const std::string& id, std::size_t line) {
    if (id.empty() || id.size() > 64)
        throw Error("fault line " + std::to_string(line) + ": invalid identifier length");
    for (const unsigned char c : id)
        if (c < 33 || c > 126 || c == '"' || c == '\\')
            throw Error("fault line " + std::to_string(line) + ": invalid identifier character");
}
inline std::vector<std::string> cells(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    do {
        const auto end = line.find('\t',start);
        out.push_back(line.substr(start,end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return out;
}
inline std::uint64_t hex(const std::string& raw, std::size_t line) {
    const std::string_view s(raw);
    const auto digits = s.starts_with("0x") || s.starts_with("0X") ? s.substr(2) : s;
    if (digits.empty() || digits.front() == '-')
        throw Error("fault line " + std::to_string(line) + ": invalid hex address/value");
    std::uint64_t n{};
    const auto [end,ec] = std::from_chars(digits.data(),digits.data()+digits.size(),n,16);
    if (ec != std::errc{} || end != digits.data()+digits.size())
        throw Error("fault line " + std::to_string(line) + ": invalid hex address/value");
    return n;
}
inline std::optional<std::uint64_t> optional_hex(const std::string& s,
                                                std::size_t line) {
    return s=="-" ? std::nullopt :
           std::optional<std::uint64_t>{hex(s,line)};
}
inline std::uint64_t decimal(const std::string& s, std::size_t line) {
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n,10);
    if (s.empty() || ec != std::errc{} || end != s.data()+s.size())
        throw Error("fault line " + std::to_string(line) + ": invalid decimal");
    return n;
}
inline void width(std::uint64_t n, unsigned bits, std::size_t line) {
    if (bits == 32 && n > UINT64_C(0xffffffff))
        throw Error("fault line " + std::to_string(line) + ": address exceeds declared width");
}
inline bool range_ok(std::uint64_t address, std::uint64_t size, unsigned bits) {
    if (size == 0) return false;
    const auto max_addr = bits==32 ? UINT64_C(0xffffffff) :
                                       std::numeric_limits<std::uint64_t>::max();
    return address <= max_addr && size - 1 <= max_addr - address;
}
inline bool contains(const FaultRegion& r, std::uint64_t addr,
                     std::uint64_t size) {
    return size != 0 && addr >= r.address &&
           addr - r.address < r.size &&
           size - 1 <= r.size - 1 - (addr - r.address);
}
inline bool overlaps(const FaultRegion& a, const FaultRegion& b) {
    if (a.address >= b.address) return a.address-b.address < b.size;
    return b.address-a.address < a.size;
}
inline std::uint8_t nibble(char c, std::size_t line) {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c-'0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c-'a'+10);
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c-'A'+10);
    throw Error("fault line " + std::to_string(line) + ": invalid memory byte");
}
inline std::vector<std::uint8_t> bytes(const std::string& raw, std::size_t line) {
    if (raw.empty() || raw.size()%2 || raw.size()>1024)
        throw Error("fault line " + std::to_string(line) + ": invalid memory hex length");
    std::vector<std::uint8_t> result;
    result.reserve(raw.size()/2);
    for (std::size_t i=0; i<raw.size(); i+=2)
        result.push_back(static_cast<std::uint8_t>(
            (nibble(raw[i],line)<<4) | nibble(raw[i+1],line)));
    return result;
}
} // namespace fault_detail

inline FaultSnapshot parse_fault_snapshot(std::istream& input, FaultLimits limits = {}) {
    if (!limits.max_input_bytes || !limits.max_line_bytes ||
        !limits.max_regions || !limits.max_threads || !limits.max_registers ||
        !limits.max_mem_samples || !limits.max_total_mem_bytes || !limits.max_findings)
        throw Error("W12 snapshot limits must be positive");
    FaultSnapshot s;
    std::size_t raw_bytes=0,line_no=0,memory_bytes=0;
    bool header=false;
    std::set<std::string> thread_ids,region_ids,heap_ids;
    std::set<std::pair<std::string,std::string>> register_ids;
    auto parse_line=[&](std::string line) {
        ++line_no;
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty() || line[0]=='#') return;
        const auto f=fault_detail::cells(line);
        const auto expect=[&](std::size_t count) {
            if (f.size()!=count)
                throw Error("fault line "+std::to_string(line_no)+": wrong field count");
        };
        if (!header) {
            expect(7);
            if(f[0]!="RTOS-OBSERVATORY-FAULT" || f[1]!="1")
                throw Error("fault line "+std::to_string(line_no)+": unsupported schema");
            fault_detail::token(f[2],line_no);
            fault_detail::token(f[4],line_no);
            fault_detail::token(f[5],line_no);
            const auto bits=fault_detail::decimal(f[3],line_no);
            if (bits!=32 && bits!=64) throw Error("W12 address width must be 32 or 64");
            if (f[6]!="claimed-coherent" && f[6]!="unverified")
                throw Error("W12 coherence must be claimed-coherent or unverified");
            s.architecture=f[2];
            s.address_bits=static_cast<unsigned>(bits);
            s.firmware_label=f[4];
            s.capture_id=f[5];
            s.claims_coherent=f[6]=="claimed-coherent";
            header=true;
            return;
        }
        if (f[0]=="REGION") {
            expect(6);
            if(s.regions.size()>=limits.max_regions)throw Error("W12 region limit reached");
            fault_detail::token(f[1],line_no);
            if (!region_ids.insert(f[1]).second) throw Error("W12 duplicate region name");
            if (f[2]!="ram" && f[2]!="flash" && f[2]!="peripheral" && f[2]!="other")
                throw Error("W12 unknown region kind");
            if (f[5]!="r" && f[5]!="rw" && f[5]!="rx" &&
                f[5]!="rwx" && f[5]!="none")
                throw Error("W12 invalid region access");
            FaultRegion region{f[1],f[2],f[5],fault_detail::hex(f[3],line_no),
                               fault_detail::hex(f[4],line_no),line_no};
            if (!fault_detail::range_ok(region.address,region.size,s.address_bits))
                throw Error("W12 region range overflow or invalid width");
            for (const auto& old : s.regions)
                if (fault_detail::overlaps(region,old))
                    throw Error("W12 overlapping memory regions have ambiguous identity");
            s.regions.push_back(std::move(region));
        } else if (f[0]=="THREAD") {
            expect(8);
            if(s.threads.size()>=limits.max_threads)throw Error("W12 thread limit reached");
            fault_detail::token(f[1],line_no);
            if(!thread_ids.insert(f[1]).second)throw Error("W12 duplicate thread");
            FaultThread thread{f[1],fault_detail::hex(f[2],line_no),
                fault_detail::hex(f[3],line_no),
                fault_detail::hex(f[4],line_no),
                fault_detail::hex(f[5],line_no),
                fault_detail::optional_hex(f[6],line_no),
                fault_detail::optional_hex(f[7],line_no),line_no};
            if (!fault_detail::range_ok(thread.tcb_address,thread.tcb_size,s.address_bits) ||
                !fault_detail::range_ok(thread.stack_address,thread.stack_size,s.address_bits))
                throw Error("W12 invalid TCB or stack allocation range");
            if(thread.observed_sp)fault_detail::width(*thread.observed_sp,s.address_bits,line_no);
            s.threads.push_back(std::move(thread));
        } else if (f[0]=="HEAP") {
            expect(6);
            if(s.heaps.size()>=limits.max_threads)
                throw Error("W12 heap metadata limit reached");
            fault_detail::token(f[1],line_no);
            if(!heap_ids.insert(f[1]).second)throw Error("W12 duplicate heap");
            FaultHeap heap{f[1],fault_detail::hex(f[2],line_no),
                fault_detail::hex(f[3],line_no),
                fault_detail::optional_hex(f[4],line_no),
                fault_detail::optional_hex(f[5],line_no),line_no};
            if(!fault_detail::range_ok(heap.address,heap.size,s.address_bits))
                throw Error("W12 invalid heap range");
            s.heaps.push_back(std::move(heap));
        } else if (f[0]=="REG") {
            expect(5);
            if(s.registers.size()>=limits.max_registers)throw Error("W12 register limit reached");
            fault_detail::token(f[1],line_no);
            fault_detail::token(f[2],line_no);
            if(!register_ids.insert({f[1],f[2]}).second)
                throw Error("W12 duplicate register snapshot entry");
            if (f[4]!="ok" && f[4]!="failed")throw Error("W12 invalid register read status");
            if ((f[4]=="failed") != (f[3]=="-"))
                throw Error("W12 register status and value disagree");
            auto value=fault_detail::optional_hex(f[3],line_no);
            if(value)fault_detail::width(*value,s.address_bits,line_no);
            s.registers.push_back({f[1],f[2],value,line_no});
        } else if (f[0]=="FAULT") {
            expect(4);
            if(s.markers.size()>=limits.max_registers)throw Error("W12 fault marker limit reached");
            fault_detail::token(f[1],line_no);
            fault_detail::token(f[2],line_no);
            auto address=fault_detail::optional_hex(f[3],line_no);
            if(address)fault_detail::width(*address,s.address_bits,line_no);
            s.markers.push_back({f[1],f[2],address,line_no});
        } else if (f[0]=="MEM") {
            expect(5);
            if(s.memory.size()>=limits.max_mem_samples)
                throw Error("W12 memory sample limit reached");
            fault_detail::token(f[1],line_no);
            if(f[4]!="ok" && f[4]!="failed")throw Error("W12 invalid memory status");
            if ((f[4]=="failed") != (f[3]=="-"))
                throw Error("W12 memory status and bytes disagree");
            const auto address=fault_detail::hex(f[2],line_no);
            fault_detail::width(address,s.address_bits,line_no);
            auto bytes=f[4]=="ok" ? fault_detail::bytes(f[3],line_no) :
                                      std::vector<std::uint8_t>{};
            if(bytes.size()>limits.max_total_mem_bytes-memory_bytes)
                throw Error("W12 memory sample byte budget exceeded");
            if(!bytes.empty() &&
               !fault_detail::range_ok(address,bytes.size(),s.address_bits))
                throw Error("W12 memory sample address range overflow");
            memory_bytes += bytes.size();
            s.memory.push_back({f[1],address,f[4]=="ok",std::move(bytes),line_no});
        } else {
            throw Error("fault line "+std::to_string(line_no)+": unsupported record type");
        }
    };
    std::string pending;
    char ch=0;
    while(input.get(ch)) {
        if(++raw_bytes>limits.max_input_bytes)throw Error("W12 input byte limit exceeded");
        if(ch=='\n') { parse_line(std::move(pending));pending.clear(); }
        else {
            if(pending.size()>=limits.max_line_bytes)
                throw Error("W12 input line limit exceeded");
            pending.push_back(ch);
        }
    }
    if(!input.eof())throw Error("W12 snapshot read failed");
    if(!pending.empty())parse_line(std::move(pending));
    if(!header)throw Error("W12 missing fault snapshot header");
    return s;
}

inline FaultView analyze_fault_snapshot(const FaultSnapshot& s, FaultLimits limits = {}) {
    if(!limits.max_findings)throw Error("W12 finding limit must be positive");
    FaultView out;
    out.claimed_coherent=s.claims_coherent;
    out.examined_records=s.regions.size()+s.threads.size()+s.heaps.size()+s.registers.size()+
                         s.markers.size()+s.memory.size();
    auto add=[&](std::string code,std::string subject,std::string message,
                 std::vector<std::size_t> lines,
                 std::optional<std::uint64_t> address={}) {
        if(out.findings.size()<limits.max_findings)
            out.findings.push_back({std::move(code),std::move(subject),
                                     std::move(message),std::move(lines),address});
        else out.findings_capped=true;
    };
    auto region_for=[&](std::uint64_t address,std::uint64_t count,
                        std::string_view kind={}) -> const FaultRegion* {
        for(const auto& r:s.regions)
            if((kind.empty() || r.kind==kind) &&
               fault_detail::contains(r,address,count))return &r;
        return nullptr;
    };
    const bool has_ram=std::any_of(s.regions.begin(),s.regions.end(),
        [](const FaultRegion& r){return r.kind=="ram";});
    const bool has_executable=std::any_of(s.regions.begin(),s.regions.end(),
        [](const FaultRegion& r){return r.access.find('x')!=std::string::npos;});
    for(const auto& thread:s.threads) {
        if(has_ram && !region_for(thread.tcb_address,thread.tcb_size,"ram"))
            add("TCB_OUTSIDE_DECLARED_RAM",thread.name,
                "Recorded TCB range is not contained in declared RAM; metadata may be incomplete",
                {thread.line},thread.tcb_address);
        if(has_ram && !region_for(thread.stack_address,thread.stack_size,"ram"))
            add("STACK_OUTSIDE_DECLARED_RAM",thread.name,
                "Recorded stack allocation not fully covered by a declared RAM region",
                {thread.line},thread.stack_address);
        const bool sp_at_empty_top =
            thread.observed_sp &&
            thread.stack_size <= std::numeric_limits<std::uint64_t>::max()-
                                  thread.stack_address &&
            *thread.observed_sp==thread.stack_address+thread.stack_size;
        if(thread.observed_sp && !sp_at_empty_top &&
           (*thread.observed_sp<thread.stack_address ||
            *thread.observed_sp-thread.stack_address>=thread.stack_size))
            add("SP_OUTSIDE_RECORDED_STACK",thread.name,
                "Recorded stack pointer is outside its declared stack allocation; "
                "bad metadata/corruption are possible, overflow is NOT proven",
                {thread.line},thread.observed_sp);
        if(thread.reported_stack_used_bytes &&
           *thread.reported_stack_used_bytes>thread.stack_size)
            add("STACK_WATERMARK_INCONSISTENT",thread.name,
                "Reported used bytes exceed recorded allocation; this is metadata disagreement",
                {thread.line},thread.stack_address);
    }
    for(const auto& heap:s.heaps) {
        if(has_ram && !region_for(heap.address,heap.size,"ram"))
            add("HEAP_OUTSIDE_DECLARED_RAM",heap.name,
                "Recorded heap range is not contained in declared RAM; map may be incomplete",
                {heap.line},heap.address);
        if(heap.reported_used_bytes && *heap.reported_used_bytes>heap.size)
            add("HEAP_USAGE_INCONSISTENT",heap.name,
                "Reported heap used bytes exceed declared capacity; metadata disagreement",
                {heap.line},heap.address);
        if(heap.reported_peak_bytes &&
           (*heap.reported_peak_bytes>heap.size ||
            (heap.reported_used_bytes &&
             *heap.reported_peak_bytes<*heap.reported_used_bytes)))
            add("HEAP_PEAK_INCONSISTENT",heap.name,
                "Reported heap peak exceeds capacity or is below current used bytes; "
                "no allocator corruption is proven",
                {heap.line},heap.address);
    }
    // Configurable fault status registers are not universal on Cortex-M,
    // especially M0/M0+ (or architecture labels with unknown profiles).
    const bool cortex_m=s.address_bits==32 &&
        (s.architecture=="cortex-m3" || s.architecture=="cortex-m4" ||
         s.architecture=="cortex-m7" || s.architecture=="cortex-m33" ||
         s.architecture=="cortex-m35p" || s.architecture=="cortex-m55" ||
         s.architecture=="cortex-m85");
    const bool cortex_m_v8=s.architecture=="cortex-m23" ||
                           s.architecture=="cortex-m33" ||
                           s.architecture=="cortex-m35p" ||
                           s.architecture=="cortex-m55" ||
                           s.architecture=="cortex-m85";
    // The source supplies values; the analyzer does not invent a fault register
    // when its read failed, nor infer a register from a generic memory dump.
    for(const auto& reg:s.registers) {
        if(!reg.value) {
            add("REGISTER_READ_FAILED",reg.context+" / "+reg.name,
                "Failed read; the register's value and implications are unknown",
                {reg.line});
            continue;
        }
        if(reg.name=="PC" && has_executable) {
            const auto* mapped=region_for(*reg.value,1);
            if(!mapped || mapped->access.find('x')==std::string::npos)
                add("PC_OUTSIDE_DECLARED_EXECUTABLE_MAP",reg.context,
                    "Observed PC is outside declared executable regions; source map "
                    "may be incomplete or the value may be corrupted",
                    {reg.line},reg.value);
        }
        if(!cortex_m)continue;
        if(reg.name=="CFSR" || reg.name=="HFSR") {
            if(*reg.value>UINT64_C(0xffffffff)) {
                add("FAULT_STATUS_WIDTH_INVALID",reg.context+" / "+reg.name,
                    "Cortex-M fault status register is 32-bit; supplied value inconsistent",
                    {reg.line},reg.value);
                continue;
            }
            static constexpr struct { unsigned bit; const char* meaning; } cfsr_bits[]={
                {0,"IACCVIOL"},{1,"DACCVIOL"},{3,"MUNSTKERR"},{4,"MSTKERR"},
                {5,"MLSPERR"},{7,"MMARVALID"},{8,"IBUSERR"},{9,"PRECISERR"},
                {10,"IMPRECISERR"},{11,"UNSTKERR"},{12,"STKERR"},{13,"LSPERR"},
                {15,"BFARVALID"},{16,"UNDEFINSTR"},{17,"INVSTATE"},
                {18,"INVPC"},{19,"NOCP"},{24,"UNALIGNED"},
                {25,"DIVBYZERO"}
            };
            static constexpr struct { unsigned bit; const char* meaning; } hfsr_bits[]={
                {1,"VECTTBL"},{30,"FORCED"},{31,"DEBUGEVT"}
            };
            if(reg.name=="CFSR") {
                if(cortex_m_v8 && (*reg.value & (UINT64_C(1)<<20))!=0)
                    add("CORTEX_M_CFSR_BIT",reg.context,
                        "Recorded Armv8-M CFSR flag: STKOF; verify exception and stack limit context",
                        {reg.line},reg.value);
                for(const auto& item:cfsr_bits)
                    if((*reg.value & (UINT64_C(1)<<item.bit))!=0)
                        add("CORTEX_M_CFSR_BIT",reg.context,
                            std::string("Recorded CFSR flag: ")+item.meaning+
                            "; this alone does not establish a root cause",
                            {reg.line},reg.value);
            } else {
                for(const auto& item:hfsr_bits)
                    if((*reg.value & (UINT64_C(1)<<item.bit))!=0)
                        add("CORTEX_M_HFSR_BIT",reg.context,
                            std::string("Recorded HFSR flag: ")+item.meaning+
                            "; this alone does not establish a root cause",
                            {reg.line},reg.value);
            }
        }
    }
    for(const auto& marker:s.markers)
        add("RECORDED_FAULT_MARKER",marker.context,
            "Source reported "+marker.type+"; exception cause and frame are not independently verified",
            {marker.line},marker.reported_address);
    for(const auto& sample:s.memory) {
        const auto r=region_for(sample.address,
                                sample.read_succeeded?sample.bytes.size():1);
        if(!sample.read_succeeded)
            add("MEMORY_READ_FAILED",sample.region,
                "Memory read explicitly failed; no bytes are assumed",
                {sample.line},sample.address);
        if(!r || r->name!=sample.region)
            add("MEMORY_SAMPLE_MAP_MISMATCH",sample.region,
                "Sample address/length does not match a declared region",
                {sample.line},sample.address);
        else if(r->kind=="peripheral")
            add("PERIPHERAL_SAMPLE_NOT_INTERPRETED",sample.region,
                "Peripheral sample is untrusted offline bytes; no live peripheral reads",
                {sample.line},sample.address);
    }
    out.warnings.push_back("Offline source claims are not independently attested: no debugger or target reads performed");
    if(!s.claims_coherent)
        out.warnings.push_back("Snapshot coherence unverified: registers and memory may be from different instants");
    if(!cortex_m)
        out.warnings.push_back("Architecture-specific exception decoding unavailable; only structural evidence is shown");
    if(!has_ram)
        out.warnings.push_back("No declared RAM ranges: TCB/stack map membership cannot be assessed");
    if(!has_executable)
        out.warnings.push_back("No executable memory ranges: PC mapping cannot be assessed");
    if(out.findings_capped)
        out.warnings.push_back("Finding list capped: unshown issues remain unknown");
    return out;
}
} // namespace observatory
