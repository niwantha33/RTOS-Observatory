// SPDX-License-Identifier: Apache-2.0
#include "observatory/engine.hpp"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
observatory::Report run(const std::string& events) {
    std::istringstream input(events);
    return observatory::analyze(observatory::parse_trace(input));
}
bool has(const observatory::Report& r, const std::string& code) {
    return std::any_of(r.findings.begin(), r.findings.end(), [&](const auto& finding) {return finding.code==code;});
}
void expect_error(const std::string& trace) {
    try { std::istringstream input(trace); (void)observatory::parse_trace(input); }
    catch (const observatory::Error&) { return; }
    assert(false && "expected invalid input to be rejected");
}
}
int main() {
    constexpr auto base = "0\tSESSION\t-\tsynthetic\t0\n"
                          "0\tCREATE\tHigh\t-\t9\n"
                          "0\tCREATE\tMedium\t-\t5\n"
                          "0\tCREATE\tLow\t-\t2\n";
    {
        auto r=run(std::string(base)+
            "1\tMUTEX_ACQUIRE\tLow\tM1\t0\n"
            "10\tMUTEX_WAIT\tHigh\tM1\t0\n"
            "20\tSWITCH_IN\tMedium\tcore0\t0\n"
            "50\tSWITCH_OUT\tMedium\tcore0\t0\n"
            "60\tMUTEX_UNLOCK\tLow\tM1\t0\n"
            "70\tMUTEX_ACQUIRE\tHigh\tM1\t0\n"
            "80\tMUTEX_UNLOCK\tHigh\tM1\t0\n");
        assert(r.complete);
        assert(r.waits.size()==1 && r.waits.front().owner=="Low" && r.waits.front().owner_proven);
        assert(has(r,"PRIORITY_INVERSION_PATTERN"));
        assert(r.intervals.size()==1 && r.intervals.front().end_us==50);
        assert(r.findings.back().observed_us==30);
        assert(observatory::report_json(r).find("\"complete\": true")!=std::string::npos);
    }
    {
        auto r=run(std::string(base)+
            "10\tMUTEX_WAIT\tHigh\tM1\t0\n"
            "20\tSWITCH_IN\tMedium\tcore0\t0\n"
            "50\tSWITCH_OUT\tMedium\tcore0\t0\n"
            "70\tMUTEX_ACQUIRE\tHigh\tM1\t0\n"
            "80\tMUTEX_UNLOCK\tHigh\tM1\t0\n");
        assert(r.complete);
        assert(!r.waits.front().owner_proven && !has(r,"PRIORITY_INVERSION_PATTERN"));
    }
    {
        auto r=run(std::string(base)+
            "1\tMUTEX_ACQUIRE\tLow\tM1\t0\n"
            "10\tMUTEX_WAIT\tHigh\tM1\t0\n"
            "11\tDROP\t-\t-\t2\n"
            "20\tSWITCH_IN\tMedium\tcore0\t0\n"
            "50\tSWITCH_OUT\tMedium\tcore0\t0\n"
            "70\tMUTEX_ACQUIRE\tHigh\tM1\t0\n"
            "80\tMUTEX_UNLOCK\tHigh\tM1\t0\n");
        assert(!r.complete && !has(r,"PRIORITY_INVERSION_PATTERN"));
    }
    {
        auto r=run(std::string(base)+
            "10\tSWITCH_IN\tHigh\tcore0\t0\n"
            "20\tISR_ENTER\t-\tcore0\t0\n"
            "25\tISR_ENTER\t-\tcore0\t0\n"
            "30\tISR_EXIT\t-\tcore0\t0\n"
            "40\tISR_EXIT\t-\tcore0\t0\n"
            "60\tSWITCH_OUT\tHigh\tcore0\t0\n");
        assert(r.complete && r.intervals.size()==3);
        assert(r.intervals[0].start_us==10 && r.intervals[0].end_us==20);
        assert(r.intervals[1].state=="isr" && r.intervals[1].start_us==20 && r.intervals[1].end_us==40);
        assert(r.intervals[2].start_us==40 && r.intervals[2].end_us==60);
    }
    {
        auto r=run(std::string(base)+
            "1\tPERIOD\tHigh\t-\t100\n"
            "100\tTASK_RELEASE\tHigh\t-\t0\n"
            "120\tTASK_BEGIN\tHigh\t-\t0\n"
            "150\tTASK_FINISH\tHigh\t-\t0\n"
            "205\tTASK_RELEASE\tHigh\t-\t0\n"
            "220\tTASK_BEGIN\tHigh\t-\t0\n"
            "240\tTASK_FINISH\tHigh\t-\t0\n");
        assert(r.complete && r.cycles.size()==2);
        assert(r.cycles[0].phase_error_us==0 && r.cycles[1].phase_error_us==5);
        assert(has(r,"DISPATCH_DELAY"));
    }
    {
        auto r=run(std::string(base)+
            "1\tWAIT_BEGIN\tHigh\tsem:S1\t0\n"
            "500\tWAIT_END\tHigh\tsem:S1\t0\n");
        assert(r.complete && r.waits.size()==1 && r.waits[0].resource_type=="semaphore");
        assert(!has(r,"PRIORITY_INVERSION_PATTERN"));
    }
    {
        // Medium execution AFTER the owner released the mutex is not
        // evidence of a priority inversion caused by that owner.
        auto r=run(std::string(base)+
            "1\tMUTEX_ACQUIRE\tLow\tM1\t0\n"
            "10\tMUTEX_WAIT\tHigh\tM1\t0\n"
            "15\tMUTEX_UNLOCK\tLow\tM1\t0\n"
            "20\tSWITCH_IN\tMedium\tcore0\t0\n"
            "50\tSWITCH_OUT\tMedium\tcore0\t0\n"
            "70\tMUTEX_ACQUIRE\tHigh\tM1\t0\n"
            "80\tMUTEX_UNLOCK\tHigh\tM1\t0\n");
        assert(r.complete && r.waits.size()==1);
        assert(r.waits.front().owner_release_us==15);
        assert(!has(r,"PRIORITY_INVERSION_PATTERN"));
    }
    {
        // If effective priority is revised during the wait, the simple
        // static-priority model must abstain instead of inventing causality.
        auto r=run(std::string(base)+
            "1\tMUTEX_ACQUIRE\tLow\tM1\t0\n"
            "10\tMUTEX_WAIT\tHigh\tM1\t0\n"
            "15\tPRIORITY\tLow\t-\t9\n"
            "20\tSWITCH_IN\tMedium\tcore0\t0\n"
            "50\tSWITCH_OUT\tMedium\tcore0\t0\n"
            "60\tMUTEX_UNLOCK\tLow\tM1\t0\n"
            "70\tMUTEX_ACQUIRE\tHigh\tM1\t0\n"
            "80\tMUTEX_UNLOCK\tHigh\tM1\t0\n");
        assert(r.complete);
        assert(!has(r,"PRIORITY_INVERSION_PATTERN"));
    }
    {
        // Missing paired schedule transitions invalidate completeness.
        auto r=run(std::string(base)+
            "1\tSWITCH_IN\tHigh\tcore0\t0\n");
        assert(!r.complete);
    }
    {
        auto r=run("0\tSESSION\t-\tsynthetic\t0\n1\tSWITCH_IN\tMissing\tcore0\t0\n");
        assert(!r.complete && !r.warnings.empty());
    }
    expect_error("0\tCREATE\tA\t-\t5\n");
    expect_error("0\tSESSION\t-\tsynthetic\t0\n2\tCREATE\tA\t-\t5\n1\tCREATE\tB\t-\t3\n");
    expect_error("0\tSESSION\t-\tsynthetic\t0\n0\tCREATE\tA\t-\t999\n");
    expect_error("0\tSESSION\t-\tsynthetic\t0\n0\tCREATE\tA\t-\t5\n1\tSESSION\t-\tx\t0\n");
    std::cout << "All Observatory core tests passed\n";
}
