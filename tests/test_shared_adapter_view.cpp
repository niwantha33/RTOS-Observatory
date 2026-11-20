// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/ui_layout.hpp"
#include "observatory/desktop/unified_view.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace observatory;
using namespace observatory::desktop;

int main() {
    // An enlarged font must reduce effective viewport capacity.
    const auto normal = plan_layout(1440.f, 18.f);
    const auto enlarged = plan_layout(1440.f, 36.f);
    assert(normal.show_sidebar);
    assert(!enlarged.show_sidebar);
    assert(normal.metric_columns >= enlarged.metric_columns);
    assert(plan_layout(300.f, 36.f).metric_columns == 1);
    assert(plan_layout(800.f, 25.f).content_width >= 0.f);
    assert(plan_layout(800.f, 25.f, false).sidebar_width == 0.f);
    assert(clamp_text_zoom(100.f) == 2.2f);
    assert(clamp_text_zoom(-10.f) == 0.75f);
    assert(clamp_text_zoom(.75f) == .75f);
    assert(clamp_text_zoom(.9f) == .9f);
    assert(clamp_text_zoom(1.0f) == 1.f);
    assert(clamp_text_zoom(NAN) == 1.f);

    Capture capture;
    capture.session = "normalized";
    capture.source = "test.trace";
    capture.events.push_back({100, "TASK_SWITCH", "worker", "-", 0, 17});
    Report report;
    report.session = "normalized";
    report.source = "test.trace";
    report.threads.push_back({"worker", 4, std::nullopt, false});
    report.intervals.push_back({"worker", "core0", "running", 100, 200});
    report.waits.push_back({"worker", "mutex:bus", "Mutex", "", 130, 160,
                            false, std::nullopt});
    report.findings.push_back({"W", "observed", "Example only", "worker",
                               "mutex:bus", 130, 160, 30});
    TimelineView timeline;
    timeline.evidence_complete = true;
    CoreUsage core;
    core.core = "core0";
    core.busy_percent = 48.0;
    timeline.cores.push_back(core);
    ResourceView typed;
    ResourceSummary pending;
    pending.name = "sem:ready";
    pending.kind = ResourceKind::Semaphore;
    pending.unresolved_waits = 1;
    pending.observed_events = 1;
    typed.resources.push_back(pending);
    auto n = project_normalized(capture, report, &timeline, true, &typed);
    assert(n.objects.size() == 1);
    assert(n.objects[0].kind == "Semaphore");
    assert(n.objects[0].unresolved_waits == 1);
    assert(n.unit == ClockUnit::Microseconds);
    assert(n.events.size() == 1 && n.events[0].source_line == 17);
    assert(n.spans[0].start == 100 && n.spans[0].end == 200);
    assert(!n.lane_index.empty());
    assert(n.event_distribution.size() == 1);
    assert(n.cases.size() == 1);
    assert(!n.cases[0].evidence_event.has_value()); // no invented causal link
    assert(has_capability(n.capabilities, ToolCapability::QualifiedCpu));
    const auto incomplete = project_normalized(capture, report, &timeline, false);
    assert(!has_capability(incomplete.capabilities, ToolCapability::QualifiedCpu));

    ctf::Session ctf;
    ctf.events.push_back({"k_sem_take", 123, 44, 7, {{"sem", "0x20"}}});
    ctf.events.push_back({"k_sem_give", 160, 77, 7, {{"sem", "0x20"}}});
    ctf.findings.push_back({"Semaphore", "sem 0x20", "Observed callback pair",
                            {0, 1}, 123, 160, true});
    ctf.thread_names[7] = "worker";
    ctf::AnalysisIndex indexed;
    indexed.thread_spans.push_back({"Thread", "worker", 123, 160, 0, 1});
    auto z = project_ctf(ctf, &indexed, "fixture");
    assert(z.unit == ClockUnit::RawCtfTicks);
    assert(z.spans.size() == 2);
    assert(z.cases.size() == 1 && z.cases[0].evidence_event == 0);
    assert(z.events[0].source_byte == 44);
    assert(!z.lane_index.empty());
    assert(z.objects.size() == 1);
    assert(z.objects[0].kind == "Semaphore");
    assert(!has_capability(z.capabilities, ToolCapability::QualifiedCpu));
    assert(has_capability(z.capabilities, ToolCapability::Resources));

    LiveSession spool;
    auto l = project_spool(spool);
    assert(l.unit == ClockUnit::PublisherTicks);
    assert(!has_capability(l.capabilities, ToolCapability::Timeline));
    assert(!has_capability(l.capabilities, ToolCapability::QualifiedCpu));
    assert(l.events.empty());
    std::cout << "Shared adapter view and responsive typography policy passed\n";
}
