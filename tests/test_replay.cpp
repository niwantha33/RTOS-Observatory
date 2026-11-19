// SPDX-License-Identifier: Apache-2.0
#include "observatory/replay.hpp"
#include "observatory/graph.hpp"
#include <cassert>
#include <iostream>
using namespace observatory;

int main() {
    DecodedTrace d;
    d.identity.clock = "ticks";
    d.evidence.sequence_available = true;
    d.capture.events = {
        {0,"SESSION","-","test",0,1},
        {1,"MUTEX_ACQUIRE","Low","M1",0,2},
        {2,"MUTEX_WAIT","High","M1",0,3},
        {2,"SWITCH_IN","Mid","cpu0",0,4},
        {3,"MUTEX_UNLOCK","Low","M1",0,5},
        {4,"MUTEX_ACQUIRE","High","M1",0,6},
        {5,"WAIT_BEGIN","Low","queue:Q",0,7},
        {6,"WAIT_END","Low","queue:Q",0,8},
    };
    const auto first = replay_at(d,0);
    assert(first.resources.empty() && first.waits.empty());
    const auto acquired = replay_at(d,2);
    assert(acquired.integrity_verified);
    assert(acquired.resources.size()==1);
    assert(acquired.resources[0].kind==ReplayResourceKind::Acquired);
    assert(acquired.resources[0].last_owner=="Low");
    assert(acquired.waits.size()==1 && acquired.waits[0].active);
    // Graph must not show later same-timestamp events beyond a replay cursor.
    auto concurrent = d;
    concurrent.capture.events[3] = {2,"WAIT_BEGIN","Mid","queue:Q",0,4};
    const auto precise = build_causal_graph(concurrent,0,2,{},GraphLimits{},2);
    const auto all_at_time = build_causal_graph(concurrent,0,2);
    assert(precise.edges.size()==6 && all_at_time.edges.size()==9);
    const auto equal_time = replay_at(d,3);
    assert(equal_time.timestamp_us==2 && equal_time.waits[0].active);
    const auto released = replay_at(d,4);
    assert(released.resources[0].kind==ReplayResourceKind::Released);
    const auto takeover = replay_at(d,5);
    assert(takeover.resources[0].kind==ReplayResourceKind::Acquired);
    assert(takeover.resources[0].last_owner=="High");
    assert(!takeover.waits[0].active);
    const auto completed_wait = replay_at(d,7);
    assert(completed_wait.waits.size()==2);
    assert(!completed_wait.waits[0].active || !completed_wait.waits[1].active);
    assert(completed_wait.clock_domain=="ticks");
    const auto capped = replay_at(d,7,3);
    assert(capped.capped && capped.applied_events==3 && capped.timestamp_us==2);
    d.evidence.missing_events=1;
    assert(!replay_at(d,5).integrity_verified);
    d.evidence.missing_events=0;
    d.capture.events.push_back({7,"DROP","-","-",2,9});
    d.capture.events.push_back({8,"MUTEX_UNLOCK","High","M1",0,10});
    const auto at_loss = replay_at(d,8);
    assert(!at_loss.integrity_verified);
    assert(at_loss.resources.size()==1);
    assert(at_loss.resources[0].kind==ReplayResourceKind::Unknown);
    assert(!at_loss.waits[0].known);
    const auto lost = replay_at(d,9);
    assert(!lost.integrity_verified);
    assert(lost.resources.size()==1 && lost.resources[0].kind==ReplayResourceKind::Unknown);
    d.capture.events.push_back({9,"MUTEX_ACQUIRE","A","M1",0,11});
    d.capture.events.push_back({10,"MUTEX_ACQUIRE","B","M1",0,12});
    const auto conflicting = replay_at(d,11);
    assert(conflicting.resources[0].kind==ReplayResourceKind::Unknown);
    assert(!conflicting.integrity_verified);
    bool rejected=false;
    try {(void)replay_at(d,99);} catch (const Error&) {rejected=true;}
    assert(rejected);
    rejected=false;
    try {(void)replay_at(d,0,0);} catch (const Error&) {rejected=true;}
    assert(rejected);
    DecodedTrace empty;
    const auto nothing = replay_at(empty,0);
    assert(nothing.resources.empty() && !nothing.warnings.empty());
    std::cout<<"W08 replay, gap, contradictory state and event navigation tests passed\n";
}
