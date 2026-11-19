// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace observatory {

// W09 reports observed numbers, not theoretical WCET or proof of RTOS causality.
// A deadline is NEVER inferred from a declared task period.
struct DiagnosticSample {
    std::string code;
    std::string task;
    std::string detail;
    std::uint64_t from_us{};
    std::uint64_t to_us{};
    std::int64_t measured_us{};
    std::optional<std::size_t> first_event;
    std::optional<std::size_t> last_event;
    bool evidence_verified{false};
};
struct TaskDiagnostic {
    std::string task;
    std::size_t releases{};
    std::size_t completed_responses{};
    std::size_t observed_interruptions{};
    std::size_t observed_deadline_misses{};
    std::optional<std::uint64_t> explicit_deadline_us;
    std::optional<std::uint64_t> max_response_us;
    std::optional<std::uint64_t> max_dispatch_delay_us;
    std::optional<std::uint64_t> max_abs_release_jitter_us;
    std::optional<std::int64_t> last_phase_drift_us;
};
struct DiagnosticsView {
    bool evidence_verified{false};
    bool rendering_limited{false};
    std::size_t examined_cycles{};
    std::vector<TaskDiagnostic> tasks;
    std::vector<DiagnosticSample> samples;
    std::vector<std::string> warnings;
};

inline DiagnosticsView analyze_scheduling(
    const DecodedTrace& trace, const Report& report,
    const std::map<std::string, std::uint64_t>& relative_deadlines_us = {},
    std::size_t max_samples = 20000) {

    if (max_samples == 0) throw Error("Diagnostic sample limit must be positive");
    for (const auto& [task, deadline] : relative_deadlines_us)
        if (task.empty() || deadline == 0 ||
            deadline > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            throw Error("Explicit deadline must be a positive signed-64-bit duration");

    DiagnosticsView out;
    out.evidence_verified = report.complete && trace.evidence.sequence_available &&
                            trace.evidence.missing_events == 0 &&
                            trace.evidence.declared_drops == 0;
    using EventKey = std::tuple<std::string, std::string, std::uint64_t>;
    std::map<EventKey, std::vector<std::size_t>> event_indices;
    for (std::size_t i = 0; i < trace.capture.events.size(); ++i) {
        const auto& e = trace.capture.events[i];
        if (e.type == "DROP") out.evidence_verified = false;
        if (e.type == "TASK_RELEASE" || e.type == "TASK_BEGIN" ||
            e.type == "TASK_FINISH" || e.type == "SWITCH_IN" ||
            e.type == "SWITCH_OUT")
            event_indices[{e.type,e.task,e.timestamp_us}].push_back(i);
    }
    std::map<EventKey, std::size_t> used;
    auto event_at = [&](const std::string& type, const std::string& task,
                        std::uint64_t t) -> std::optional<std::size_t> {
        const EventKey k{type,task,t};
        const auto it = event_indices.find(k);
        if (it == event_indices.end()) return {};
        const auto n = used[k]++;
        if (n >= it->second.size()) return {};
        return it->second[n];
    };
    std::map<std::string, TaskDiagnostic> summaries;
    std::map<std::string, std::uint64_t> periods;
    std::map<std::string, std::uint64_t> last_release;
    for (const auto& thread : report.threads) {
        summaries[thread.id].task = thread.id;
        if (thread.period_us && *thread.period_us) periods[thread.id] = *thread.period_us;
    }
    for (const auto& [task, deadline] : relative_deadlines_us) {
        summaries[task].task = task;
        summaries[task].explicit_deadline_us = deadline;
    }
    auto emit = [&](DiagnosticSample sample) {
        if (out.samples.size() >= max_samples) {
            out.rendering_limited = true;
            return;
        }
        out.samples.push_back(std::move(sample));
    };
    for (const auto& cycle : report.cycles) {
        ++out.examined_cycles;
        auto& summary = summaries[cycle.task];
        summary.task = cycle.task;
        ++summary.releases;
        const auto release = event_at("TASK_RELEASE", cycle.task, cycle.release_us);
        const auto begin = cycle.start_us ?
            event_at("TASK_BEGIN", cycle.task, *cycle.start_us) : std::optional<std::size_t>{};
        const auto finish = cycle.end_us ?
            event_at("TASK_FINISH", cycle.task, *cycle.end_us) : std::optional<std::size_t>{};
        const auto verified = [&](bool has_end) {
            return out.evidence_verified && release.has_value() && has_end;
        };
        if (const auto prev = last_release.find(cycle.task); prev != last_release.end()) {
            const auto period = periods.find(cycle.task);
            if (period != periods.end() && cycle.release_us >= prev->second) {
                const auto actual = cycle.release_us - prev->second;
                const auto expected = period->second;
                if (actual <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                    expected <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    const auto signed_delta = static_cast<std::int64_t>(actual) -
                                              static_cast<std::int64_t>(expected);
                    const auto absolute = signed_delta >= 0 ?
                        static_cast<std::uint64_t>(signed_delta) :
                        static_cast<std::uint64_t>(-signed_delta);
                    if (!summary.max_abs_release_jitter_us ||
                        absolute > *summary.max_abs_release_jitter_us)
                        summary.max_abs_release_jitter_us = absolute;
                    emit({"RELEASE_JITTER",cycle.task,
                          "Signed difference between consecutive observed releases and declared period",
                          prev->second,cycle.release_us,signed_delta,{},release,verified(release.has_value())});
                }
            }
        }
        last_release[cycle.task] = cycle.release_us;
        if (cycle.phase_error_us) {
            summary.last_phase_drift_us = *cycle.phase_error_us;
            emit({"RELEASE_PHASE_DRIFT",cycle.task,
                  "Relative to first observed release, not an external oscillator",
                  cycle.release_us,cycle.release_us,*cycle.phase_error_us,
                  release,release,verified(release.has_value())});
        }
        if (cycle.start_us && *cycle.start_us >= cycle.release_us) {
            const auto observed = *cycle.start_us - cycle.release_us;
            if (!summary.max_dispatch_delay_us || observed > *summary.max_dispatch_delay_us)
                summary.max_dispatch_delay_us = observed;
            emit({"APPLICATION_BEGIN_DELAY",cycle.task,
                  "Release to TASK_BEGIN marker; NOT necessarily scheduler switch-in latency",
                  cycle.release_us,*cycle.start_us,static_cast<std::int64_t>(observed),
                  release,begin,verified(begin.has_value())});
        }
        if (cycle.start_us && cycle.end_us && *cycle.end_us >= *cycle.start_us &&
            *cycle.end_us >= cycle.release_us) {
            const auto response = *cycle.end_us - cycle.release_us;
            ++summary.completed_responses;
            if (!summary.max_response_us || response > *summary.max_response_us)
                summary.max_response_us = response;
            emit({"OBSERVED_RESPONSE",cycle.task,
                  "Recorded release to TASK_FINISH (wall time); observed max is not theoretical WCET",
                  cycle.release_us,*cycle.end_us,static_cast<std::int64_t>(response),
                  release,finish,verified(finish.has_value())});
            if (summary.explicit_deadline_us && response > *summary.explicit_deadline_us) {
                ++summary.observed_deadline_misses;
                emit({"EXPLICIT_DEADLINE_EXCEEDED",cycle.task,
                      "Observed response exceeded explicitly supplied relative deadline",
                      cycle.release_us,*cycle.end_us,static_cast<std::int64_t>(
                          response - *summary.explicit_deadline_us),
                      release,finish,verified(finish.has_value())});
            }
        }
    }

    // A different task executing between two observed segments is an
    // interruption pattern, NOT proof that the earlier task remained ready.
    std::map<std::pair<std::string,std::string>,std::vector<const Interval*>> by_task_core;
    std::map<std::string,std::vector<const Interval*>> by_core;
    for (const auto& span : report.intervals) {
        if (span.state != "running" || span.task.empty() || span.core.empty() ||
            span.end_us <= span.start_us) continue;
        by_task_core[{span.task,span.core}].push_back(&span);
        by_core[span.core].push_back(&span);
    }
    for (auto& [_, spans] : by_core) {
        std::sort(spans.begin(),spans.end(),[](auto* a,auto* b) {
            return std::tie(a->start_us,a->end_us) < std::tie(b->start_us,b->end_us);
        });
    }
    std::size_t comparisons = 0;
    constexpr std::size_t max_comparisons = 200000;
    for (auto& [key, spans] : by_task_core) {
        std::sort(spans.begin(),spans.end(),[](auto* a,auto* b) {
            return std::tie(a->start_us,a->end_us) < std::tie(b->start_us,b->end_us);
        });
        const auto& core = by_core[key.second];
        for (std::size_t i=1; i<spans.size(); ++i) {
            const auto* before = spans[i-1];
            const auto* after = spans[i];
            if (before->end_us >= after->start_us) continue;
            // Bound scans even if a pathological trace has many alternating intervals.
            for (const auto* other : core) {
                if (++comparisons > max_comparisons) {
                    out.rendering_limited = true;
                    break;
                }
                if (other->start_us < before->end_us) continue;
                if (other->start_us >= after->start_us) break;
                if (other->task == key.first || other->end_us > after->start_us) continue;
                const auto switched_out = event_at("SWITCH_OUT",key.first,before->end_us);
                const auto switched_in = event_at("SWITCH_IN",key.first,after->start_us);
                if (!switched_out || !switched_in) continue;
                auto& summary = summaries[key.first];
                summary.task = key.first;
                ++summary.observed_interruptions;
                emit({"EXECUTION_INTERRUPTION_PATTERN",key.first,
                      "Another task ("+other->task+") ran between observed segments on "+key.second+
                      "; readiness/preemption cause is unknown",
                      before->end_us,after->start_us,
                      static_cast<std::int64_t>(after->start_us - before->end_us),
                      switched_out,switched_in,out.evidence_verified});
                break;
            }
            if (comparisons > max_comparisons) break;
        }
        if (comparisons > max_comparisons) break;
    }
    for (auto& [_, entry] : summaries) out.tasks.push_back(std::move(entry));
    if (!out.evidence_verified)
        out.warnings.emplace_back("Missing/incomplete trace evidence: reported numbers are observed samples, not certified complete history");
    if (relative_deadlines_us.empty())
        out.warnings.emplace_back("No explicit relative deadlines supplied; deadline miss status is unknown");
    if (out.rendering_limited)
        out.warnings.emplace_back("Diagnostic preview or interruption scan capped; omitted candidates are unknown");
    out.warnings.emplace_back("TASK_BEGIN is an application marker; observed maximum response is not theoretical WCET");
    return out;
}
} // namespace observatory
