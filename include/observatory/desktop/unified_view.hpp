// SPDX-License-Identifier: Apache-2.0
#pragma once
// Adapter-neutral *presentation* contract. Native units and source provenance
// remain attached: this is NOT a conversion of CTF ticks into microseconds,
// nor an assertion of equivalent kernel instrumentation across RTOSes.
#include "observatory/engine.hpp"
#include "observatory/timeline.hpp"
#include "observatory/ctf_analysis.hpp"
#include "observatory/live_session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace observatory::desktop {
enum class AdapterKind { None, Normalized, ZephyrCtf, BinarySpool };
enum class ClockUnit { Microseconds, RawCtfTicks, PublisherTicks };
enum class ToolCapability : std::uint32_t {
    Events = 1u, Timeline = 2u, Resources = 4u, Findings = 8u,
    Scheduling = 16u, QualifiedCpu = 32u, SourceEvidence = 64u
};
inline bool has_capability(std::uint32_t bits, ToolCapability flag) noexcept {
    return (bits & static_cast<std::uint32_t>(flag)) != 0;
}
struct ToolEvent {
    std::string title;
    std::string actor;
    std::uint64_t tick{};
    std::size_t source_index{};
    std::optional<std::size_t> source_byte;
    std::optional<std::size_t> source_line;
    std::uint32_t epoch{}; // nonzero only for W18 publisher records
    std::vector<std::pair<std::string, std::string>> fields;
};
struct ToolSpan {
    std::string lane;
    std::string category; // Thread, ISR, Wait, Mutex, Semaphore, Queue...
    std::uint64_t start{}, end{};
    std::optional<std::size_t> evidence_event;
};
struct ToolCase {
    std::string kind, title, assessment;
    std::uint64_t start{}, end{};
    std::optional<std::size_t> evidence_event;
    bool completion_observed{};
};
struct ToolFinding {
    std::string title, detail, confidence;
    std::optional<std::size_t> evidence_event;
};
struct ToolSummary {
    AdapterKind adapter{AdapterKind::None};
    ClockUnit unit{ClockUnit::RawCtfTicks};
    std::uint32_t capabilities{};
    std::string name, source, clock_label, evidence_warning;
    std::size_t observed_threads{}, total_events{}, total_findings{};
    bool loss_qualified{false};
    bool clock_calibrated{false};
    bool records_limited{false};
    std::map<std::string, std::vector<std::size_t>> lane_index;
    std::vector<std::pair<std::string, std::size_t>> event_distribution;
    std::vector<ToolEvent> events;
    std::vector<ToolSpan> spans;
    std::vector<ToolCase> cases;
    std::vector<ToolFinding> findings;
    std::vector<std::pair<std::string, double>> qualified_cpu; // use only qualified source
};
inline std::uint32_t caps(std::initializer_list<ToolCapability> list) {
    std::uint32_t flags = 0;
    for (const auto capability : list)
        flags |= static_cast<std::uint32_t>(capability);
    return flags;
}

// Build presentation indices once per import/refresh, not on every frame.
// This limits UI rendering to already bounded and source-proven observations.
inline void finish_projection(ToolSummary& view) {
    for (std::size_t i = 0; i < view.spans.size(); ++i)
        view.lane_index[view.spans[i].lane].push_back(i);
    std::map<std::string, std::size_t> frequencies;
    for (const auto& event : view.events)
        ++frequencies[event.title];
    view.event_distribution.assign(frequencies.begin(), frequencies.end());
    std::sort(view.event_distribution.begin(), view.event_distribution.end(),
              [](const auto& left, const auto& right) {
                  return left.second > right.second;
              });
}

inline ToolSummary project_normalized(const Capture& capture, const Report& report,
                                      const TimelineView* timeline,
                                      bool loss_qualified) {
    ToolSummary v;
    v.adapter = AdapterKind::Normalized;
    v.name = report.session;
    v.source = capture.source;
    v.unit = ClockUnit::Microseconds;
    v.clock_label = "microseconds from normalized trace";
    v.capabilities = caps({ToolCapability::Events, ToolCapability::Timeline,
                           ToolCapability::Resources, ToolCapability::Findings,
                           ToolCapability::SourceEvidence});
    v.loss_qualified = loss_qualified;
    v.clock_calibrated = false; // normalized units do not prove physical clock calibration
    v.observed_threads = report.threads.size();
    v.total_events = capture.events.size();
    v.total_findings = report.findings.size();
    v.evidence_warning = loss_qualified ? "Normalized trace; qualified observed intervals"
                                        : "Trace continuity or coverage not qualified";
    constexpr std::size_t event_limit = 20000, span_limit = 10000, case_limit = 5000;
    for (std::size_t i = 0; i < std::min(capture.events.size(), event_limit); ++i) {
        const auto& e = capture.events[i];
        ToolEvent row;
        row.title = e.type;
        row.actor = e.task;
        row.tick = e.timestamp_us;
        row.source_index = i;
        row.source_line = e.line;
        row.fields = {{"type", e.type}, {"task", e.task}, {"object", e.object},
                      {"value", std::to_string(e.value)}};
        v.events.push_back(std::move(row));
    }
    // Keep half-open interval bounds as recorded by the normalized analyzer.
    for (const auto& span : report.intervals) {
        if (v.spans.size() >= span_limit) { v.records_limited = true; break; }
        if (span.end_us < span.start_us) continue;
        v.spans.push_back({span.core + " / " + span.task, span.state,
                           span.start_us, span.end_us, std::nullopt});
    }
    for (const auto& wait : report.waits) {
        if (v.cases.size() >= case_limit) { v.records_limited = true; break; }
        if (wait.end_us < wait.start_us) continue;
        ToolCase item;
        item.kind = wait.resource_type.empty() ? "Wait" : wait.resource_type;
        item.title = wait.resource;
        item.assessment = wait.owner_proven ? "Recorded owner evidence" :
                                                "Owner not proven by this trace";
        item.start = wait.start_us;
        item.end = wait.end_us;
        item.completion_observed = true; // Report::Wait is a paired completed wait
        v.cases.push_back(item);
        if (v.spans.size() < span_limit)
            v.spans.push_back({wait.task + " / " + wait.resource, item.kind,
                               wait.start_us, wait.end_us, std::nullopt});
    }
    for (const auto& f : report.findings) {
        if (v.findings.size() >= 3000) { v.records_limited = true; break; }
        v.findings.push_back({f.code, f.description, f.confidence, std::nullopt});
    }
    if (timeline && timeline->evidence_complete && loss_qualified) {
        for (const auto& core : timeline->cores)
            if (core.busy_percent)
                v.qualified_cpu.emplace_back(core.core, *core.busy_percent);
        if (!v.qualified_cpu.empty())
            v.capabilities |= static_cast<std::uint32_t>(ToolCapability::QualifiedCpu);
    }
    if (v.events.size() < capture.events.size() || v.spans.size() >= span_limit ||
        v.cases.size() >= case_limit)
        v.records_limited = true;
    finish_projection(v);
    return v;
}

inline ToolSummary project_ctf(const ctf::Session& session,
                               const ctf::AnalysisIndex* analysis,
                               const std::string& capture_name = "Zephyr CTF") {
    ToolSummary v;
    v.adapter = AdapterKind::ZephyrCtf;
    v.name = capture_name;
    v.source = "Local Zephyr CTF";
    v.clock_label = "uncalibrated CTF ticks";
    v.unit = ClockUnit::RawCtfTicks;
    v.capabilities = caps({ToolCapability::Events, ToolCapability::Timeline,
                           ToolCapability::Resources, ToolCapability::Findings,
                           ToolCapability::Scheduling, ToolCapability::SourceEvidence});
    v.clock_calibrated = session.clock_calibrated;
    v.loss_qualified = session.loss_qualified;
    v.observed_threads = session.thread_names.size();
    v.total_events = session.events.size();
    v.total_findings = session.findings.size();
    v.evidence_warning = "CTF ticks are not CPU utilization; event loss unknown; "
                         "matching callbacks are correlation only";
    constexpr std::size_t event_limit = 20000, span_limit = 10000, case_limit = 5000;
    for (std::size_t i = 0; i < std::min(session.events.size(), event_limit); ++i) {
        const auto& e = session.events[i];
        ToolEvent row;
        row.title = e.name;
        row.tick = e.tick;
        row.source_index = i;
        row.source_byte = e.byte_offset;
        if (e.actor >= 0)
            row.actor = std::to_string(e.actor);
        row.fields.assign(e.fields.begin(), e.fields.end());
        v.events.push_back(std::move(row));
    }
    if (analysis) {
        const auto append = [&](const std::vector<ctf::AnalysisSpan>& spans) {
            for (const auto& s : spans) {
                if (v.spans.size() >= span_limit) { v.records_limited = true; break; }
                if (s.end_tick < s.start_tick) continue;
                const auto evidence = s.end_event < v.events.size()
                    ? std::optional<std::size_t>{s.end_event} : std::nullopt;
                v.spans.push_back({s.kind + " / " + s.subject, s.kind,
                                   s.start_tick, s.end_tick, evidence});
            }
        };
        append(analysis->thread_spans);
        append(analysis->isr_spans);
        v.records_limited |= analysis->truncated;
    }
    for (const auto& f : session.findings) {
        if (v.cases.size() >= case_limit) { v.records_limited = true; break; }
        if (f.end_tick < f.start_tick) continue;
        const auto evidence = !f.evidence.empty() && f.evidence.front() < v.events.size()
            ? std::optional<std::size_t>{f.evidence.front()} : std::nullopt;
        v.cases.push_back({f.kind, f.title, f.assessment, f.start_tick,
                           f.end_tick, evidence, f.complete});
        v.findings.push_back({f.title, f.assessment, "correlation only", evidence});
        if (v.spans.size() < span_limit)
            v.spans.push_back({"Resource / " + f.kind, f.kind, f.start_tick,
                               f.end_tick, evidence});
    }
    if (session.events.size() > event_limit || session.findings.size() > case_limit)
        v.records_limited = true;
    finish_projection(v);
    return v;
}

inline ToolSummary project_spool(const LiveSession& source,
                                 const std::string& name = "Binary event spool") {
    ToolSummary v;
    v.adapter = AdapterKind::BinarySpool;
    v.name = name;
    v.source = "Local W18 binary spool";
    v.clock_label = "per-epoch publisher ticks, no cross-epoch timeline";
    v.unit = ClockUnit::PublisherTicks;
    v.capabilities = caps({ToolCapability::Events, ToolCapability::SourceEvidence});
    const auto& status = source.status();
    v.total_events = static_cast<std::size_t>(status.total_events);
    v.loss_qualified = false; // wire/status counters cannot prove loss-free capture
    v.clock_calibrated = false;
    v.evidence_warning = "Numeric event types have no RTOS meaning without an approved "
                         "decoder; no scheduling or CPU calculations";
    v.records_limited = status.history_pruned != 0 || status.incomplete;
    std::size_t index = 0;
    for (const auto& point : source.recent()) {
        const auto& record = point.event;
        ToolEvent e;
        e.title = "Event ID " + std::to_string(record.event.event_type);
        e.actor = "Task ID " + std::to_string(record.event.task_id);
        e.tick = record.event.timestamp;
        e.epoch = point.epoch;
        e.source_index = index++;
        e.fields = {{"lane", std::to_string(record.lane)},
                    {"sequence", std::to_string(record.sequence)},
                    {"core ID", std::to_string(record.event.core_id)},
                    {"object ID", std::to_string(record.event.object_id)}};
        v.events.push_back(std::move(e));
    }
    finish_projection(v);
    return v;
}
} // namespace observatory::desktop
