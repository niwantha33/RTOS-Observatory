// SPDX-License-Identifier: Apache-2.0
#include "observatory/diagnostics.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
using namespace observatory;

static bool has(const DiagnosticsView& view, const std::string& code) {
    return std::any_of(view.samples.begin(), view.samples.end(),
                       [&](const auto& item) { return item.code == code; });
}
int main() {
    DecodedTrace trace;
    trace.evidence.sequence_available = true;
    trace.identity.clock = "mono";
    trace.capture.events = {
        {0,"SESSION","-","synthetic",0,1},
        {10,"TASK_RELEASE","A","-",0,2},
        {20,"TASK_BEGIN","A","-",0,3},
        {50,"TASK_FINISH","A","-",0,4},
        {110,"TASK_RELEASE","A","-",0,5},
        {140,"TASK_BEGIN","A","-",0,6},
        {185,"TASK_FINISH","A","-",0,7},
        {215,"TASK_RELEASE","A","-",0,8}
    };
    Report report;
    report.complete = true;
    report.threads = {{"A",9,100,false}};
    report.cycles = {
        {"A",10,20,50,0},
        {"A",110,140,185,0},
        {"A",215,{}, {},5}
    };
    const auto normal = analyze_scheduling(trace,report,{{"A",60}});
    assert(normal.evidence_verified && !normal.rendering_limited);
    assert(normal.tasks.size()==1);
    const auto& a=normal.tasks[0];
    assert(a.task=="A" && a.releases==3 && a.completed_responses==2);
    assert(a.max_response_us && *a.max_response_us==75);
    assert(a.max_dispatch_delay_us && *a.max_dispatch_delay_us==30);
    assert(a.max_abs_release_jitter_us && *a.max_abs_release_jitter_us==5);
    assert(a.last_phase_drift_us && *a.last_phase_drift_us==5);
    assert(a.explicit_deadline_us && *a.explicit_deadline_us==60);
    assert(a.observed_deadline_misses==1);
    assert(has(normal,"RELEASE_JITTER") && has(normal,"RELEASE_PHASE_DRIFT"));
    assert(has(normal,"APPLICATION_BEGIN_DELAY") && has(normal,"OBSERVED_RESPONSE"));
    assert(has(normal,"EXPLICIT_DEADLINE_EXCEEDED"));
    const auto deadline = std::find_if(normal.samples.begin(),normal.samples.end(),
        [](const auto& item) { return item.code=="EXPLICIT_DEADLINE_EXCEEDED"; });
    assert(deadline!=normal.samples.end() && deadline->measured_us==15);
    assert(deadline->first_event && *deadline->first_event==4);
    assert(deadline->last_event && *deadline->last_event==6 && deadline->evidence_verified);

    const auto no_deadline = analyze_scheduling(trace,report);
    assert(!has(no_deadline,"EXPLICIT_DEADLINE_EXCEEDED"));
    assert(no_deadline.tasks[0].observed_deadline_misses==0);
    const auto clipped = analyze_scheduling(trace,report,{{"A",60}},1);
    assert(clipped.rendering_limited && clipped.samples.size()==1);
    assert(clipped.tasks[0].max_response_us && *clipped.tasks[0].max_response_us==75);
    trace.evidence.declared_drops=3;
    const auto missing = analyze_scheduling(trace,report,{{"A",60}});
    assert(!missing.evidence_verified);
    assert(std::all_of(missing.samples.begin(),missing.samples.end(),
        [](const DiagnosticSample& s){return !s.evidence_verified;}));
    trace.evidence.declared_drops=0;
    report.complete = false;
    assert(!analyze_scheduling(trace,report,{{"A",60}}).evidence_verified);

    // Scheduler pattern, not a proof that A was ready while B ran.
    Report interrupted;
    interrupted.intervals = {
        {"A","cpu0","running",10,30},
        {"B","cpu0","running",30,40},
        {"A","cpu0","running",40,50},
        {"A","cpu1","running",25,30}
    };
    DecodedTrace scheduler;
    scheduler.evidence.sequence_available = true;
    scheduler.capture.events = {
        {0,"SESSION","-","pattern",0,1},
        {30,"SWITCH_OUT","A","cpu0",0,2},
        {40,"SWITCH_IN","A","cpu0",0,3}
    };
    auto candidate=analyze_scheduling(scheduler,interrupted);
    assert(has(candidate,"EXECUTION_INTERRUPTION_PATTERN"));
    const auto observed = std::find_if(candidate.samples.begin(),candidate.samples.end(),
        [](const auto& item){return item.code=="EXECUTION_INTERRUPTION_PATTERN";});
    assert(observed != candidate.samples.end() && observed->measured_us==10);
    assert(observed->evidence_verified && observed->first_event && observed->last_event);
    assert(observed->detail.find("unknown") != std::string::npos);

    bool failed=false;
    try {(void)analyze_scheduling(trace,report,{{"A",0}});}
    catch(const Error&) {failed=true;}
    assert(failed);
    failed=false;
    try {(void)analyze_scheduling(trace,report,{},0);}
    catch(const Error&) {failed=true;}
    assert(failed);

    Report empty_report;
    DecodedTrace empty_capture;
    const auto empty = analyze_scheduling(empty_capture,empty_report);
    assert(!empty.evidence_verified);
    assert(empty.tasks.empty() && empty.samples.empty());
    std::cout<<"W09 observed jitter, response, explicit deadlines and interruption tests passed\n";
}
