// SPDX-License-Identifier: Apache-2.0
#include "observatory/resource_analysis.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
using namespace observatory;

static const ResourceSummary& resource(const ResourceView& view, const std::string& name) {
    const auto it = std::find_if(view.resources.begin(), view.resources.end(),
                                [&](const auto& r) { return r.name == name; });
    assert(it != view.resources.end());
    return *it;
}
static bool anomaly(const ResourceView& view, const std::string& code) {
    return std::any_of(view.anomalies.begin(), view.anomalies.end(),
                       [&](const auto& a) { return a.code == code; });
}
int main() {
    DecodedTrace d;
    d.identity.clock = "mono";
    d.identity.rtos = "synthetic-demo";
    d.capture.source = "synthetic-fixture";
    d.evidence.sequence_available = true;
    d.capture.events = {
        {0, "SESSION", "-", "fixture", 0, 1},
        {1, "MUTEX_ACQUIRE", "Low", "mutex:M1", 0, 2},
        {2, "MUTEX_WAIT", "High", "mutex:M1", 0, 3},
        {3, "WAIT_BEGIN", "A", "sem:S", 0, 4},
        {4, "WAIT_END", "A", "sem:S", 0, 5},
        {5, "WAIT_BEGIN", "B", "queue:Q", 0, 6},
        {6, "WAIT_BEGIN", "C", "gate:G", 0, 7},
        {7, "WAIT_BEGIN", "D", "mailbox:B", 0, 8},
        {8, "WAIT_BEGIN", "E", "event:V", 0, 9},
        {9, "WAIT_BEGIN", "F", "notify:N", 0, 10},
        {10, "MUTEX_UNLOCK", "Low", "mutex:M1", 0, 11},
        {11, "MUTEX_ACQUIRE", "High", "mutex:M1", 0, 12},
        {12, "MUTEX_UNLOCK", "High", "mutex:M1", 0, 13},
        {13, "WAIT_END", "B", "queue:Q", 0, 14},
        {14, "WAIT_END", "C", "gate:G", 0, 15},
        {15, "WAIT_END", "D", "mailbox:B", 0, 16},
        {16, "WAIT_END", "E", "event:V", 0, 17},
        {17, "WAIT_END", "F", "notify:N", 0, 18}
    };
    const auto view = analyze_resource_observations(d);
    assert(view.evidence_verified && !view.limited);
    assert(view.clock_domain == "mono" && view.rtos_label == "synthetic-demo");
    assert(view.resources.size() == 7 && view.observations.size() == 17);
    const auto& mutex = resource(view,"mutex:M1");
    assert(mutex.kind == ResourceKind::Mutex);
    assert(mutex.observed_wait_starts == 1 && mutex.completed_waits == 1);
    assert(mutex.max_completed_wait_us && *mutex.max_completed_wait_us == 9);
    assert(!mutex.last_recorded_owner && !mutex.owner_observation_valid);
    assert(!mutex.continuity_lost);
    const auto& sem = resource(view,"sem:S");
    assert(sem.kind == ResourceKind::Semaphore);
    assert(sem.max_completed_wait_us && *sem.max_completed_wait_us == 1);
    assert(!sem.last_recorded_owner && !sem.owner_observation_valid);
    assert(resource(view,"queue:Q").kind == ResourceKind::Queue);
    assert(resource(view,"gate:G").kind == ResourceKind::Gate);
    assert(resource(view,"mailbox:B").kind == ResourceKind::Mailbox);
    assert(resource(view,"event:V").kind == ResourceKind::Event);
    assert(resource(view,"notify:N").kind == ResourceKind::Notification);
    assert(view.anomalies.empty());
    const auto finish = std::find_if(view.observations.begin(), view.observations.end(),
        [](const auto& e) {return e.type == "MUTEX_ACQUIRE" && e.task == "High";});
    assert(finish != view.observations.end() && finish->completed_wait_us &&
           *finish->completed_wait_us == 9 && finish->wait_start_event &&
           *finish->wait_start_event == 2);
    assert(finish->event_index == 11 && finish->source_line == 12);
    assert(finish->evidence_verified);

    // Missing matching begin must NOT imply a completed wait.
    d.capture.events.push_back({18,"WAIT_END","A","sem:S",0,19});
    const auto orphan = analyze_resource_observations(d);
    assert(anomaly(orphan,"UNPAIRED_WAIT_END"));
    assert(resource(orphan,"sem:S").completed_waits == 1);

    d.capture.events.push_back({19,"MUTEX_ACQUIRE","Owner","M2",0,20});
    d.capture.events.push_back({20,"MUTEX_ACQUIRE","Other","M2",0,21});
    d.capture.events.push_back({21,"MUTEX_UNLOCK","Owner","M2",0,22});
    const auto conflicting = analyze_resource_observations(d);
    assert(anomaly(conflicting,"AMBIGUOUS_MUTEX_ACQUIRE"));
    assert(anomaly(conflicting,"UNMATCHED_MUTEX_UNLOCK"));
    assert(!resource(conflicting,"M2").last_recorded_owner);

    d.capture.events.push_back({22,"WAIT_BEGIN","A","sem:S",0,23});
    d.capture.events.push_back({23,"WAIT_BEGIN","A","sem:S",0,24});
    const auto duplicate = analyze_resource_observations(d);
    assert(anomaly(duplicate,"DUPLICATE_WAIT_BEGIN"));
    assert(resource(duplicate,"sem:S").unresolved_waits >= 2);

    // Explicit loss leaves prior ownership unknown and never fabricates a wait end.
    DecodedTrace gaps;
    gaps.evidence.sequence_available = true;
    gaps.capture.events = {
        {0,"SESSION","-","gap",0,1},
        {1,"MUTEX_ACQUIRE","Low","M1",0,2},
        {2,"MUTEX_WAIT","High","M1",0,3},
        {3,"DROP","-","-",4,4}
    };
    const auto lost = analyze_resource_observations(gaps);
    assert(!lost.evidence_verified);
    assert(resource(lost,"M1").continuity_lost);
    assert(!resource(lost,"M1").owner_observation_valid);
    assert(!resource(lost,"M1").last_recorded_owner);
    assert(resource(lost,"M1").unresolved_waits == 1);
    gaps.evidence.missing_events = 1;
    assert(!analyze_resource_observations(gaps).evidence_verified);

    // Invalid direct input must not create fake resource nodes.
    DecodedTrace malformed;
    malformed.evidence.sequence_available = true;
    malformed.capture.events = {
        {0,"SESSION","-","invalid",0,1},
        {1,"WAIT_BEGIN","-","queue:Q",0,2},
        {2,"MUTEX_ACQUIRE","A","-",0,3}
    };
    const auto invalid = analyze_resource_observations(malformed);
    assert(invalid.resources.empty() && invalid.anomalies.size() == 2);
    assert(anomaly(invalid,"INVALID_RESOURCE_EVENT"));

    const auto capped = analyze_resource_observations(d,true,{4,1000,10000,100,100});
    assert(capped.limited && capped.examined_events == 4);
    assert(resource(capped,"sem:S").unresolved_waits == 1);
    const auto small_display = analyze_resource_observations(d,true,{100,1000,10000,1,100});
    assert(small_display.limited && small_display.observations.size() == 1);
    assert(resource(small_display,"mutex:M1").completed_waits == 1);
    const auto small_resource = analyze_resource_observations(d,true,{100,1,10000,100,100});
    assert(small_resource.limited && small_resource.resources.size() == 1);
    bool threw = false;
    try { (void)analyze_resource_observations(d,true,{0,1,1,1,1}); }
    catch (const Error&) { threw = true; }
    assert(threw);

    const auto empty = analyze_resource_observations(DecodedTrace{});
    assert(empty.resources.empty() && !empty.evidence_verified);
    std::cout << "W10 bounded offline resource semantics and evidence tests passed\n";
}
