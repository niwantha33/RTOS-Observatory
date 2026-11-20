// SPDX-License-Identifier: Apache-2.0
#include "observatory/ctf_analysis.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace observatory::ctf {
namespace {
std::string thread_label(const Session& source, const Event& event) {
    const auto id = describe_field(event, "thread_id");
    if (id.empty()) return {};
    try {
        const auto numeric = std::stoull(id);
        auto it = source.thread_names.find(numeric);
        if (it != source.thread_names.end() && !it->second.empty())
            return it->second + " [" + id + "]";
    } catch (...) {
        return {}; // Invalid source ID is not silently trusted.
    }
    return "thread " + id;
}
bool scheduler_marker(const std::string& name) {
    return name == "thread_ready" || name == "thread_sched_ready" ||
           name == "thread_sched_pend" || name == "thread_sched_wakeup" ||
           name == "thread_sched_priority_set" || name == "thread_sched_lock" ||
           name == "thread_sched_unlock" || name == "thread_switched_in" ||
           name == "thread_switched_out";
}
} // namespace

AnalysisIndex build_analysis_index(const Session& source, std::size_t maximum_events,
                                   std::size_t maximum_spans) {
    if (maximum_events == 0 || maximum_spans == 0)
        throw std::invalid_argument("CTF index limits must be positive");
    AnalysisIndex result;
    result.resource_cases = source.findings.size();
    // No loss-free proof is available from a simple CTF saved channel.
    result.time_calibrated = false;
    result.full_coverage_verified = false;
    const auto bound = std::min(source.events.size(), maximum_events);
    result.truncated = bound < source.events.size();
    std::optional<std::pair<std::size_t, std::string>> running;
    std::vector<std::size_t> isrs;
    for (std::size_t i = 0; i < bound; ++i) {
        const auto& event = source.events[i];
        if (scheduler_marker(event.name) &&
            result.scheduler_events.size() < maximum_spans)
            result.scheduler_events.push_back(i);
        if (event.name == "thread_switched_in") {
            if (running) ++result.unpaired_thread_switches;
            auto name = thread_label(source, event);
            if (name.empty()) {
                ++result.unpaired_thread_switches;
                running.reset();
            } else {
                running = std::make_pair(i, std::move(name));
            }
        } else if (event.name == "thread_switched_out") {
            if (running && describe_field(source.events[running->first], "thread_id") ==
                               describe_field(event, "thread_id") &&
                event.tick >= source.events[running->first].tick) {
                if (result.thread_spans.size() < maximum_spans) {
                    const auto start = running->first;
                    result.thread_spans.push_back(
                        {"Thread", running->second, source.events[start].tick, event.tick,
                         start, i});
                } else {
                    result.truncated = true;
                }
            } else {
                ++result.unpaired_thread_switches;
            }
            running.reset();
        } else if (event.name == "isr_enter") {
            if (isrs.size() >= 32) {
                ++result.unpaired_isr_callbacks;
                isrs.clear();
                result.truncated = true;
            } else {
                isrs.push_back(i);
            }
        } else if (event.name == "isr_exit" || event.name == "isr_exit_to_scheduler") {
            if (isrs.empty()) {
                ++result.unpaired_isr_callbacks;
            } else {
                const auto first = isrs.back();
                isrs.pop_back();
                if (event.tick < source.events[first].tick) {
                    ++result.unpaired_isr_callbacks;
                } else if (result.isr_spans.size() < maximum_spans) {
                    result.isr_spans.push_back({"ISR", "identity unknown",
                        source.events[first].tick, event.tick, first, i});
                } else {
                    result.truncated = true;
                }
            }
        }
    }
    result.unpaired_thread_switches += running.has_value() ? 1u : 0u;
    result.unpaired_isr_callbacks += isrs.size();
    // A complete local input file is not proof of a complete hardware capture.
    return result;
}
} // namespace observatory::ctf
