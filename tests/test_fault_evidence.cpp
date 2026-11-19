// SPDX-License-Identifier: Apache-2.0
#include "observatory/fault_evidence.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace observatory;

static std::string fixture() {
    return
        "RTOS-OBSERVATORY-FAULT\t1\tcortex-m4\t32\tfw-synthetic-v1\tcrash-demo\tunverified\n"
        "REGION\tflash\tflash\t0x08000000\t0x20000\trx\n"
        "REGION\tram\tram\t0x20000000\t0x8000\trw\n"
        "REGION\tperiph\tperipheral\t0x40000000\t0x100\trw\n"
        "THREAD\tControl\t0x20000020\t0x80\t0x20001000\t0x800\t0x200017fc\t0x400\n"
        "THREAD\tFaulted\t0x20000200\t0x80\t0x20002000\t0x800\t0x20003000\t0x900\n"
        "REG\tcpu0\tPC\t0x08000400\tok\n"
        "REG\tcpu0\tCFSR\t0x200\tok\n"
        "REG\tcpu0\tHFSR\t0x40000000\tok\n"
        "REG\tcpu0\tBFAR\t-\tfailed\n"
        "REG\tcpu1\tPC\t0x40000002\tok\n"
        "FAULT\tHardFault\tcpu0\t0x08000400\n"
        "MEM\tram\t0x20001000\tdeadbeef\tok\n"
        "MEM\tram\t0x20001004\t-\tfailed\n"
        "MEM\tperiph\t0x40000000\t1234\tok\n"
        "MEM\tghost\t0x20001008\taabb\tok\n";
}
static FaultSnapshot decode(const std::string& raw, FaultLimits limits={}) {
    std::istringstream input(raw);
    return parse_fault_snapshot(input,limits);
}
static bool has(const FaultView& v, const std::string& code) {
    return std::any_of(v.findings.begin(),v.findings.end(),
        [&](const FaultFinding& f) {return f.code==code;});
}
static bool reject(const std::string& raw, FaultLimits limits={}) {
    try { (void)decode(raw,limits); }
    catch (const Error&) {return true;}
    return false;
}

int main() {
    const auto snapshot=decode(fixture());
    assert(snapshot.architecture=="cortex-m4" && snapshot.address_bits==32);
    assert(snapshot.firmware_label=="fw-synthetic-v1");
    assert(snapshot.capture_id=="crash-demo" && !snapshot.claims_coherent);
    assert(snapshot.regions.size()==3 && snapshot.threads.size()==2);
    assert(snapshot.registers.size()==5 && snapshot.markers.size()==1);
    assert(snapshot.memory.size()==4);
    assert(snapshot.threads[0].observed_sp && *snapshot.threads[0].observed_sp==0x200017fc);
    assert(snapshot.threads[0].reported_stack_used_bytes &&
           *snapshot.threads[0].reported_stack_used_bytes==0x400);
    assert(snapshot.memory[0].bytes.size()==4 &&
           snapshot.memory[0].bytes[0]==0xde &&
           snapshot.memory[0].bytes[1]==0xad &&
           snapshot.memory[0].bytes[2]==0xbe &&
           snapshot.memory[0].bytes[3]==0xef);
    assert(!snapshot.memory[1].read_succeeded && snapshot.memory[1].bytes.empty());
    const auto view=analyze_fault_snapshot(snapshot);
    assert(!view.independently_attested && !view.claimed_coherent);
    assert(view.examined_records==15);
    assert(has(view,"SP_OUTSIDE_RECORDED_STACK"));
    assert(has(view,"STACK_WATERMARK_INCONSISTENT"));
    assert(has(view,"CORTEX_M_CFSR_BIT"));
    assert(has(view,"CORTEX_M_HFSR_BIT"));
    assert(has(view,"REGISTER_READ_FAILED"));
    assert(has(view,"MEMORY_READ_FAILED"));
    assert(has(view,"PC_OUTSIDE_DECLARED_EXECUTABLE_MAP"));
    assert(has(view,"RECORDED_FAULT_MARKER"));
    assert(has(view,"PERIPHERAL_SAMPLE_NOT_INTERPRETED"));
    assert(has(view,"MEMORY_SAMPLE_MAP_MISMATCH"));
    assert(!has(view,"TCB_OUTSIDE_DECLARED_RAM"));
    const auto cfsr=std::find_if(view.findings.begin(),view.findings.end(),
        [](const auto& f){return f.code=="CORTEX_M_CFSR_BIT";});
    assert(cfsr!=view.findings.end());
    assert(cfsr->explanation.find("PRECISERR")!=std::string::npos);
    assert(cfsr->source_lines.size()==1 && cfsr->source_lines[0]==8);
    assert(!view.warnings.empty());

    const auto claimed=decode(
        "RTOS-OBSERVATORY-FAULT\t1\tcortex-m4\t32\tfw\tone\tclaimed-coherent\n"
        "REGION\tram\tram\t0x20000000\t0x1000\trw\n"
        "THREAD\tT\t0x20000010\t0x20\t0x20000800\t0x100\t-\t-\n");
    const auto cv=analyze_fault_snapshot(claimed);
    assert(cv.claimed_coherent && !cv.independently_attested);
    assert(!has(cv,"SP_OUTSIDE_RECORDED_STACK"));
    assert(!has(cv,"TCB_OUTSIDE_DECLARED_RAM"));
    assert(!has(cv,"STACK_OUTSIDE_DECLARED_RAM"));
    assert(cv.findings.empty());

    const auto unknown=decode(
        "RTOS-OBSERVATORY-FAULT\t1\triscv32\t32\tfw\tone\tunverified\n"
        "REG\tcpu0\tCFSR\t0x200\tok\n"
        "REG\tcpu0\tPC\t-\tfailed\n");
    const auto uv=analyze_fault_snapshot(unknown);
    assert(!has(uv,"CORTEX_M_CFSR_BIT"));
    assert(has(uv,"REGISTER_READ_FAILED"));
    assert(!has(uv,"PC_OUTSIDE_DECLARED_EXECUTABLE_MAP"));

    const auto edge=decode(
        "RTOS-OBSERVATORY-FAULT\t1\tcustom64\t64\tfw\tone\tunverified\n"
        "REGION\tram\tram\t0xfffffffffffffff0\t0x10\trw\n"
        "THREAD\tT\t0xfffffffffffffff0\t0x1\t0xfffffffffffffff8\t0x8\t0xffffffffffffffff\t-\n");
    assert(!has(analyze_fault_snapshot(edge),"SP_OUTSIDE_RECORDED_STACK"));

    const auto cap=analyze_fault_snapshot(snapshot,FaultLimits{1024*1024,4096,128,1024,2048,256,64*1024,1});
    assert(cap.findings_capped && cap.findings.size()==1);
    assert(reject("",{}));
    assert(reject("RTOS-OBSERVATORY-FAULT\t2\tcortex-m4\t32\tfw\tone\tunverified\n"));
    assert(reject("RTOS-OBSERVATORY-FAULT\t1\tcortex-m4\t16\tfw\tone\tunverified\n"));
    assert(reject("RTOS-OBSERVATORY-FAULT\t1\tcortex-m4\t32\tfw\tone\tcertified\n"));
    const auto base=std::string("RTOS-OBSERVATORY-FAULT\t1\tcortex-m4\t32\tfw\tone\tunverified\n");
    assert(reject(base+"REGION\tram\tram\t0x20000000\t0x0\trw\n"));
    assert(reject(base+"REGION\tram\tram\t0xfffffff0\t0x20\trw\n"));
    assert(reject(base+"REGION\tram\tram\t0x20000000\t0x100\trw\n"
                       "REGION\talias\tram\t0x20000080\t0x100\trw\n"));
    assert(reject(base+"REGION\tram\tram\t0x20000000\t0x100\trw\n"
                       "REGION\tram\tram\t0x20000200\t0x100\trw\n"));
    assert(reject(base+"THREAD\tT\t0x20000000\t0x100\t0x20001000\t0x100\t0x100000000\t-\n"));
    assert(reject(base+"THREAD\tT\t0x20000000\t0x100\t0x20001000\t0x100\t-\t-\n"
                       "THREAD\tT\t0x20000100\t0x100\t0x20002000\t0x100\t-\t-\n"));
    assert(reject(base+"REG\tcpu0\tPC\t-\tok\n"));
    assert(reject(base+"REG\tcpu0\tPC\t0x1\tfailed\n"));
    assert(reject(base+"REG\tcpu0\tPC\t0x1\tok\n"
                       "REG\tcpu0\tPC\t0x2\tok\n"));
    assert(reject(base+"MEM\tram\t0x20000000\t123\tok\n"));
    assert(reject(base+"MEM\tram\t0x20000000\tgh\tok\n"));
    assert(reject(base+"MEM\tram\t0x20000000\t1234\tfailed\n"));
    assert(reject(base+"MEM\tram\t0xffffffff\tdeadbeef\tok\n"));
    assert(reject(base+"UNKNOWN\tfoo\n"));
    assert(reject(base+"FAULT\tHardFault\tcpu0\n"));
    auto too_tight=FaultLimits{};
    too_tight.max_total_mem_bytes=2;
    assert(reject(fixture(),too_tight));
    too_tight=FaultLimits{};
    too_tight.max_registers=1;
    assert(reject(fixture(),too_tight));
    too_tight=FaultLimits{};
    too_tight.max_input_bytes=20;
    assert(reject(fixture(),too_tight));
    too_tight=FaultLimits{};
    too_tight.max_line_bytes=20;
    assert(reject(fixture(),too_tight));
    too_tight=FaultLimits{};
    too_tight.max_threads=1;
    assert(reject(fixture(),too_tight));
    std::cout<<"W12 offline fault snapshot, TCB, stack, Cortex-M and malformed-input tests passed\n";
}
