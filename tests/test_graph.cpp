// SPDX-License-Identifier: Apache-2.0
#include "observatory/graph.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace observatory;

int main() {
    DecodedTrace trace;
    trace.identity.clock = "mono";
    trace.capture.source = "synthetic-offline";
    trace.evidence.sequence_available = true;
    trace.capture.events = {
        {0,"SESSION","-","demo",0,1},
        {10,"CREATE","T1","-",3,2},
        {20,"MUTEX_WAIT","T1","mutex:A",0,3},
        {20,"MUTEX_ACQUIRE","T1","mutex:A",0,4},
        {21,"MUTEX_UNLOCK","T1","mutex:A",0,5},
        {22,"WAIT_BEGIN","T2","queue:Q",0,6},
        {30,"WAIT_END","T2","queue:Q",0,7}
    };
    const auto graph = build_causal_graph(trace, 0, 30);
    assert(graph.evidence_complete && !graph.truncated);
    assert(graph.nodes.size() == 9); // 2 tasks + 2 resources + 5 evidence
    assert(graph.edges.size() == 15);
    assert(graph.clock_domain == "mono");
    assert(graph.edges[0].relation == "MUTEX_WAIT");
    assert(graph.edges[0].timestamp_us == 20);
    assert(graph.edges[0].source_line == 3);
    assert(graph.edges[3].relation == "MUTEX_ACQUIRE");
    assert(graph.edges[3].source_line == 4); // equal timestamp preserves input order
    assert(graph.nodes[2].event_index && *graph.nodes[2].event_index == 2);
    const auto only_t2 = build_causal_graph(trace, 0, 30, "T2");
    assert(only_t2.nodes.size() == 4 && only_t2.edges.size() == 6);
    const auto one_instant = build_causal_graph(trace, 20, 20);
    assert(one_instant.edges.size() == 6); // half-open window would lose evidence
    const auto empty = build_causal_graph(trace, 100, 101);
    assert(empty.nodes.empty() && !empty.warnings.empty());
    const auto capped = build_causal_graph(trace, 0, 30, {}, {4, 6, 100});
    assert(capped.truncated);
    assert(capped.nodes.size() == 4 && capped.edges.size() == 6);
    const auto scan_capped = build_causal_graph(trace, 0, 30, {}, {360,900,3});
    assert(scan_capped.truncated && scan_capped.edges.size() == 3);
    trace.evidence.missing_events = 2;
    const auto missing = build_causal_graph(trace, 0, 30);
    assert(!missing.evidence_complete && !missing.warnings.empty());
    trace.evidence.missing_events = 0;
    trace.evidence.sequence_available = false;
    assert(!build_causal_graph(trace, 0, 30).evidence_complete);
    trace.evidence.sequence_available = true;
    trace.capture.events.push_back({31,"DROP","-","-",3,8});
    assert(!build_causal_graph(trace, 0, 40).evidence_complete);
    trace.capture.events.push_back({32,"MUTEX_WAIT","-","-",0,9});
    trace.capture.events.push_back({33,"WAIT_BEGIN","","",0,10});
    const auto malformed = build_causal_graph(trace, 31, 40);
    assert(malformed.nodes.empty()); // invalid task/resource pairs are never linked
    bool failed = false;
    try { (void)build_causal_graph(trace, 30, 20); }
    catch (const Error&) { failed = true; }
    assert(failed);
    failed = false;
    try { (void)build_causal_graph(trace, 0, 30, {}, {2,3,100}); }
    catch (const Error&) { failed = true; }
    assert(failed);
    std::cout << "W07 bounded evidence-linked graph tests passed\n";
}
