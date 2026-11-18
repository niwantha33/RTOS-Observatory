// SPDX-License-Identifier: Apache-2.0
#include "observatory/timeline.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace observatory;
int main() {
    Report r;
    r.start_us = 0;
    r.end_us = 100;
    r.threads = {{"A", 5, {}, false}, {"idle0", 0, {}, true}};
    r.intervals = {{"A","cpu0","running",10,40},
                   {"__ISR__","cpu0","isr",40,50},
                   {"idle0","cpu0","idle",50,80}};
    r.waits = {{"A","mutex:1","mutex","B",5,15,false,{}}};
    const auto partial = build_timeline(r,0,100,true);
    assert(partial.cores.size() == 1);
    assert(partial.cores[0].running_us == 30);
    assert(partial.cores[0].isr_us == 10);
    assert(partial.cores[0].idle_us == 30);
    assert(partial.cores[0].unknown_us == 30);
    assert(!partial.cores[0].busy_percent);
    assert(partial.tasks[0].running_us == 30);
    assert(partial.tasks[0].completed_wait_us == 10);
    assert(partial.tasks[0].completed_waits == 1);
    assert(partial.slices.size() == 5);
    assert(partial.slices.front().state == "unknown");
    const auto covered = build_timeline(r,10,80,true);
    assert(covered.cores[0].unknown_us == 0);
    assert(covered.cores[0].busy_percent);
    assert(std::abs(*covered.cores[0].busy_percent - 4000.0/70.0) < 0.0001);
    const auto loss = build_timeline(r,10,80,false);
    assert(!loss.cores[0].busy_percent);
    r.complete = false;
    assert(!build_timeline(r,10,80,true).cores[0].busy_percent);
    r.complete = true;
    const auto capped = build_timeline(r,0,100,true,1);
    assert(capped.rendering_limited && capped.slices.size() == 1);
    assert(capped.cores[0].running_us == 30);
    bool rejected = false;
    try { (void)build_timeline(r,50,50,true); } catch (const Error&) { rejected=true; }
    assert(rejected);
    rejected = false;
    try { (void)build_timeline(r,1,101,true); } catch (const Error&) { rejected=true; }
    assert(rejected);
    r.intervals = {{"A","cpu0","running",0,60},
                   {"B","cpu0","running",20,40},
                   {"idle0","cpu0","idle",60,100}};
    const auto conflicted = build_timeline(r,0,100,true);
    assert(conflicted.cores[0].running_us == 40);
    assert(conflicted.cores[0].unknown_us == 20);
    assert(!conflicted.cores[0].busy_percent);
    // Two independent CPUs are NEVER merged into one utilization denominator.
    r.intervals = {{"A","cpu0","running",0,100},
                   {"idle0","cpu1","idle",0,100}};
    const auto multicore = build_timeline(r,0,100,true);
    assert(multicore.cores.size() == 2);
    assert(*multicore.cores[0].busy_percent == 100.0);
    assert(*multicore.cores[1].busy_percent == 0.0);
    r.intervals = {{"A","cpu0","running",70,90}};
    const auto unobserved = build_timeline(r,0,50,true);
    assert(unobserved.cores.size() == 1);
    assert(unobserved.cores[0].unknown_us == 50);
    assert(unobserved.cores[0].running_us == 0);
    assert(!unobserved.cores[0].busy_percent);
    std::cout << "W06 interval, gap, ISR, wait, overlap and CPU tests passed\n";
}
