// SPDX-License-Identifier: Apache-2.0
#include "observatory/ctf_analysis.hpp"
#include <cassert>
#include <stdexcept>
using namespace observatory::ctf;
namespace {
Event e(const char *name, std::uint64_t tick, const char *tid = nullptr) {
    Event event;
    event.name = name;
    event.tick = tick;
    if (tid)
        event.fields.emplace("thread_id", tid);
    return event;
}
} // namespace
int main() {
    Session s;
    s.thread_names.emplace(3, "worker");
    s.events = {
        e("thread_switched_in", 100, "3"),
        e("isr_enter", 110),
        e("isr_exit", 125),
        e("thread_ready", 130, "7"),
        e("thread_switched_out", 150, "3"),
        e("thread_switched_in", 160, "3"),
    };
    auto result = build_analysis_index(s);
    assert(result.thread_spans.size() == 1);
    assert(result.thread_spans[0].begin_event == 0);
    assert(result.thread_spans[0].end_event == 4);
    assert(result.thread_spans[0].start_tick == 100 && result.thread_spans[0].end_tick == 150);
    assert(result.thread_spans[0].subject == "worker [3]");
    assert(result.isr_spans.size() == 1);
    assert(result.isr_spans[0].begin_event == 1 && result.isr_spans[0].end_event == 2);
    assert(result.unpaired_thread_switches == 1);
    assert(result.scheduler_events.size() == 4);
    assert(!result.time_calibrated && !result.full_coverage_verified);
    assert(!result.truncated);

    s.events.insert(s.events.begin() + 3, e("isr_exit", 128));
    auto unmatched = build_analysis_index(s);
    assert(unmatched.unpaired_isr_callbacks == 1);
    auto limited = build_analysis_index(s, 2);
    assert(limited.truncated);
    assert(limited.unpaired_thread_switches == 1);
    bool rejected = false;
    try {
        (void)build_analysis_index(s, 0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected);
    return 0;
}
