// SPDX-License-Identifier: Apache-2.0
#include "observatory/timeline.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace observatory {
namespace {
struct Edge {
    std::uint64_t time{};
    std::size_t span{};
    bool begin{};
};
void add_render(TimelineView& out, TimelineSlice slice, std::size_t limit) {
    if (slice.start_us >= slice.end_us) return;
    if (!out.rendering_limited && !out.slices.empty()) {
        auto& last = out.slices.back();
        if (last.core == slice.core && last.task == slice.task &&
            last.state == slice.state && last.end_us == slice.start_us) {
            last.end_us = slice.end_us;
            return;
        }
    }
    if (out.slices.size() < limit) out.slices.push_back(std::move(slice));
    else out.rendering_limited = true;
}
} // namespace

TimelineView build_timeline(const Report& report, std::uint64_t start_us,
                            std::uint64_t end_us, bool evidence_complete,
                            std::size_t render_limit) {
    if (start_us >= end_us || start_us < report.start_us || end_us > report.end_us)
        throw Error("Timeline range must be non-empty and inside recorded capture boundaries");
    TimelineView out;
    out.start_us = start_us;
    out.end_us = end_us;
    out.evidence_complete = evidence_complete && report.complete;
    const auto window_us = end_us - start_us;
    std::map<std::string, std::vector<const Interval*>> per_core;
    std::map<std::string, TaskUsage> per_task;
    for (const auto& task : report.threads) per_task[task.id] = {task.id};
    for (const auto& span : report.intervals) {
        if (span.end_us <= start_us || span.start_us >= end_us) continue;
        if (span.end_us <= span.start_us || span.core.empty()) continue;
        per_core[span.core].push_back(&span);
    }
    for (const auto& [name, spans] : per_core) {
        CoreUsage usage;
        usage.core = name;
        std::vector<Edge> edges;
        edges.reserve(spans.size() * 2 + 2);
        edges.push_back({start_us, 0, false});
        edges.push_back({end_us, 0, false});
        for (std::size_t i = 0; i < spans.size(); ++i) {
            edges.push_back({std::max(spans[i]->start_us, start_us), i, true});
            edges.push_back({std::min(spans[i]->end_us, end_us), i, false});
        }
        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return a.time < b.time;
        });
        std::set<std::size_t> active;
        std::uint64_t cursor = start_us;
        std::size_t pos = 0;
        while (pos < edges.size()) {
            const auto now = edges[pos].time;
            if (now > cursor) {
                std::string state = "unknown";
                std::string task;
                if (active.size() == 1) {
                    const auto& span = *spans[*active.begin()];
                    if (span.state == "running" || span.state == "idle" || span.state == "isr") {
                        state = span.state;
                        task = span.task;
                    }
                }
                const auto duration = now - cursor;
                if (state == "running") {
                    usage.running_us += duration;
                    auto& entry = per_task[task];
                    entry.task = task;
                    entry.running_us += duration;
                } else if (state == "idle") usage.idle_us += duration;
                else if (state == "isr") usage.isr_us += duration;
                else usage.unknown_us += duration;
                add_render(out, {name, task, state, cursor, now}, render_limit);
            }
            // Apply all transitions at a timestamp together: equal-time
            // switch-out/switch-in never creates a fake overlap or gap.
            while (pos < edges.size() && edges[pos].time == now) {
                if (edges[pos].begin) active.insert(edges[pos].span);
                else active.erase(edges[pos].span);
                ++pos;
            }
            cursor = now;
        }
        if (usage.unknown_us == 0 && out.evidence_complete)
            usage.busy_percent = 100.0 * static_cast<double>(usage.running_us + usage.isr_us) /
                                 static_cast<double>(window_us);
        out.cores.push_back(std::move(usage));
    }
    for (const auto& w : report.waits) {
        const auto from = std::max(start_us, w.start_us);
        const auto to = std::min(end_us, w.end_us);
        if (to <= from) continue;
        auto& entry = per_task[w.task];
        entry.task = w.task;
        entry.completed_wait_us += to - from;
        ++entry.completed_waits;
        if (out.waits.size() < render_limit)
            out.waits.push_back({w.task, w.resource, w.resource_type, from, to});
        else out.rendering_limited = true;
    }
    for (const auto& [id, task] : per_task) {
        (void)id;
        out.tasks.push_back(task);
    }
    if (per_core.empty()) out.warnings.push_back("No observed scheduler intervals for this time window");
    if (!out.evidence_complete)
        out.warnings.push_back("Incomplete or unverifiable evidence: exact CPU percentage suppressed");
    if (out.rendering_limited)
        out.warnings.push_back("Rendering capped; CPU and task totals still include all observed intervals");
    for (const auto& c : out.cores) if (c.unknown_us)
        out.warnings.push_back("Core " + c.core + " contains unknown time; it is not assumed idle");
    return out;
}
} // namespace observatory
