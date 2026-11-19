// SPDX-License-Identifier: Apache-2.0
#include "observatory/causal_diagnostics.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <string>
using namespace observatory;

static const CausalFinding* find(const CausalView& view,const std::string& code) {
    const auto it=std::find_if(view.findings.begin(),view.findings.end(),
        [&](const auto& f) {return f.code==code;});
    return it == view.findings.end() ? nullptr : &*it;
}
int main() {
    // The synthetic high/medium/low mutex scenario has known causal markers.
    DecodedTrace d;
    d.evidence.sequence_available = true;
    d.capture.events = {
        {0,"SESSION","-","fixture",0,1},
        {0,"CREATE","High","-",9,2},
        {0,"CREATE","Medium","-",5,3},
        {0,"CREATE","Low","-",2,4},
        {10,"MUTEX_ACQUIRE","Low","M1",0,5},
        {100,"TASK_RELEASE","High","-",0,6},
        {110,"MUTEX_WAIT","High","M1",0,7},
        {120,"SWITCH_IN","Medium","core0",0,8},
        {150,"SWITCH_OUT","Medium","core0",0,9},
        {155,"MUTEX_UNLOCK","Low","M1",0,10},
        {160,"MUTEX_ACQUIRE","High","M1",0,11},
        {170,"TASK_BEGIN","High","-",0,12},
        {180,"TASK_FINISH","High","-",0,13},
        {190,"MUTEX_UNLOCK","High","M1",0,14}
    };
    Report report;
    report.complete = true;
    report.intervals = {{"Medium","core0","running",120,150}};
    report.cycles = {{"High",100,170,180,0}};
    const auto normal=analyze_causal_patterns(d,report,40);
    assert(normal.evidence_complete && !normal.limited);
    const auto inversion = find(normal,"PRIORITY_INVERSION_EXECUTION_PATTERN");
    assert(inversion && inversion->confidence=="observed-pattern");
    assert(inversion->task=="High" && inversion->related_task=="Low");
    assert(inversion->observed_us==30);
    assert(std::find(inversion->evidence_events.begin(),
                     inversion->evidence_events.end(),4)!=inversion->evidence_events.end());
    assert(std::find(inversion->evidence_events.begin(),
                     inversion->evidence_events.end(),6)!=inversion->evidence_events.end());
    assert(std::find(inversion->evidence_events.begin(),
                     inversion->evidence_events.end(),9)!=inversion->evidence_events.end());
    assert(std::find(inversion->evidence_events.begin(),
                     inversion->evidence_events.end(),7)!=inversion->evidence_events.end());
    const auto drift = find(normal,"MUTEX_WAIT_IN_APPLICATION_BEGIN_DELAY");
    assert(drift && drift->observed_us==70 && drift->evidence_events.size()>=4);
    const auto long_wait=find(normal,"LONG_COMPLETED_MUTEX_WAIT");
    assert(long_wait && long_wait->observed_us==50);
    assert(!find(normal,"LONG_UNCLOSED_MUTEX_WAIT"));
    auto no_medium=report;
    no_medium.intervals.clear();
    assert(!find(analyze_causal_patterns(d,no_medium),"PRIORITY_INVERSION_EXECUTION_PATTERN"));
    auto short_wait=analyze_causal_patterns(d,report,60);
    assert(!find(short_wait,"LONG_COMPLETED_MUTEX_WAIT"));
    auto priority_changed=d;
    priority_changed.capture.events.insert(
        priority_changed.capture.events.begin()+8,
        {130,"PRIORITY","High","-",1,99});
    const auto revised=analyze_causal_patterns(priority_changed,report);
    assert(!find(revised,"PRIORITY_INVERSION_EXECUTION_PATTERN"));
    auto late_medium=report;
    late_medium.intervals = {{"Medium","core0","running",160,180}};
    assert(!find(analyze_causal_patterns(d,late_medium),
                 "PRIORITY_INVERSION_EXECUTION_PATTERN"));
    d.evidence.missing_events=2;
    const auto loss=analyze_causal_patterns(d,report);
    assert(!loss.evidence_complete);
    assert(find(loss,"PRIORITY_INVERSION_EXECUTION_PATTERN"));
    assert(find(loss,"PRIORITY_INVERSION_EXECUTION_PATTERN")->confidence=="unverified");
    d.evidence.missing_events=0;
    // A->M2->B->M1->A: snapshot circular wait, not a proven hardware deadlock.
    DecodedTrace cycle;
    cycle.evidence.sequence_available=true;
    cycle.capture.events={
        {0,"SESSION","-","deadlock-demo",0,1},
        {0,"CREATE","A","-",4,2},
        {0,"CREATE","B","-",3,3},
        {1,"MUTEX_ACQUIRE","A","M1",0,4},
        {2,"MUTEX_ACQUIRE","B","M2",0,5},
        {3,"MUTEX_WAIT","A","M2",0,6},
        {4,"MUTEX_WAIT","B","M1",0,7}
    };
    const auto circular=analyze_causal_patterns(cycle,Report{});
    const auto deadlock=find(circular,"CIRCULAR_MUTEX_WAIT_CANDIDATE");
    assert(deadlock && deadlock->confidence=="candidate");
    assert(deadlock->evidence_events.size()>=3);
    assert(std::find(deadlock->evidence_events.begin(),
                     deadlock->evidence_events.end(),6)!=deadlock->evidence_events.end());
    assert(std::find(deadlock->evidence_events.begin(),
                     deadlock->evidence_events.end(),3)!=deadlock->evidence_events.end());
    assert(std::find(deadlock->evidence_events.begin(),
                     deadlock->evidence_events.end(),4)!=deadlock->evidence_events.end());
    auto broken=cycle;
    broken.capture.events.insert(broken.capture.events.begin()+6,
                                 {4,"DROP","-","-",1,99});
    const auto after_loss=analyze_causal_patterns(broken,Report{});
    assert(!after_loss.evidence_complete);
    assert(!find(after_loss,"CIRCULAR_MUTEX_WAIT_CANDIDATE"));

    DecodedTrace starving;
    starving.evidence.sequence_available=true;
    starving.capture.events={
        {0,"SESSION","-","starvation-demo",0,1},
        {10,"MUTEX_WAIT","A","M1",0,2},
        {50,"TASK_RELEASE","A","-",0,3}
    };
    assert(find(analyze_causal_patterns(starving,Report{},20),
                "LONG_UNCLOSED_MUTEX_WAIT"));
    assert(!find(analyze_causal_patterns(starving,Report{}),
                 "LONG_UNCLOSED_MUTEX_WAIT"));
    const auto scanned=analyze_causal_patterns(starving,Report{},20,{2,2000,1000,20000});
    assert(scanned.limited);
    assert(!find(scanned,"LONG_UNCLOSED_MUTEX_WAIT"));
    bool rejected=false;
    try { (void)analyze_causal_patterns(d,report,0,{0,1,1,1}); }
    catch(const Error&) { rejected=true; }
    assert(rejected);
    assert(analyze_causal_patterns(DecodedTrace{},Report{}).findings.empty());
    std::cout<<"W11 inversion, cycle, drift, loss and starvation candidate tests passed\n";
}
