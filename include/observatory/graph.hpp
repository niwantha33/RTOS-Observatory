// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace observatory {

// W07: event-backed relationships, never inferred historical ownership or causality.
enum class GraphNodeKind { Task, Resource, Evidence };
struct GraphNode {
    std::string id;
    std::string label;
    GraphNodeKind kind{};
    std::optional<std::size_t> event_index;
};
struct GraphEdge {
    std::size_t from{};
    std::size_t to{};
    std::string relation; // exact recorded event type, or "supports"
    std::size_t event_index{};
    std::size_t source_line{};
    std::uint64_t timestamp_us{};
};
struct GraphLimits {
    std::size_t max_nodes{360};
    std::size_t max_edges{900};
    std::size_t max_scanned_events{100000};
};
struct GraphView {
    std::string clock_domain;
    std::string source;
    std::uint64_t begin_us{};
    std::uint64_t end_us{};
    bool evidence_complete{false};
    bool truncated{false};
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::vector<std::string> warnings;
};

// Inclusive timestamps: equal-timestamp events retain decoder/file order.
// This bounded *display model* makes no claim of an exhaustive resource graph.
inline GraphView build_causal_graph(const DecodedTrace& trace,
                                   std::uint64_t begin_us, std::uint64_t end_us,
                                   std::string_view task_filter = {},
                                   GraphLimits limits = {}) {
    if (begin_us > end_us)
        throw Error("Graph time window must have start <= end");
    if (limits.max_nodes < 3 || limits.max_edges < 3 || !limits.max_scanned_events)
        throw Error("Graph limits must allow at least one event relationship");
    GraphView out;
    out.clock_domain = trace.identity.clock.empty() ? "unspecified" : trace.identity.clock;
    out.source = trace.capture.source;
    out.begin_us = begin_us;
    out.end_us = end_us;
    out.evidence_complete = trace.evidence.sequence_available &&
                            trace.evidence.missing_events == 0 &&
                            trace.evidence.declared_drops == 0;
    std::map<std::string, std::size_t> by_id;
    const auto& events = trace.capture.events;
    auto add_node = [&](std::string id, std::string label, GraphNodeKind kind,
                        std::optional<std::size_t> event_index) {
        const auto found = by_id.find(id);
        if (found != by_id.end()) return found->second;
        const auto position = out.nodes.size();
        by_id.emplace(id, position);
        out.nodes.push_back({std::move(id), std::move(label), kind, event_index});
        return position;
    };
    const auto scan_count = std::min(events.size(), limits.max_scanned_events);
    for (std::size_t i = 0; i < scan_count; ++i) {
        const auto& event = events[i];
        if (event.type == "DROP") out.evidence_complete = false;
        if (event.timestamp_us < begin_us || event.timestamp_us > end_us) continue;
        const bool mutex_event = event.type == "MUTEX_WAIT" ||
                                 event.type == "MUTEX_ACQUIRE" ||
                                 event.type == "MUTEX_UNLOCK";
        const bool generic_event = event.type == "WAIT_BEGIN" ||
                                   event.type == "WAIT_END";
        if (!mutex_event && !generic_event) continue;
        if (event.task.empty() || event.task == "-" ||
            event.object.empty() || event.object == "-") continue;
        if (!task_filter.empty() && event.task.find(task_filter) == std::string::npos)
            continue;
        const std::string task_id = "task:" + event.task;
        const std::string resource_id = "resource:" + event.object;
        const std::string evidence_id = "event:" + std::to_string(i);
        const std::size_t new_nodes =
            static_cast<std::size_t>(!by_id.contains(task_id)) +
            static_cast<std::size_t>(!by_id.contains(resource_id)) + 1;
        if (new_nodes > limits.max_nodes - out.nodes.size() ||
            3 > limits.max_edges - out.edges.size()) {
            out.truncated = true;
            break; // no partial relationship or dangling evidence edge
        }
        const auto task = add_node(task_id, event.task, GraphNodeKind::Task, {});
        const auto resource = add_node(resource_id, event.object,
                                       GraphNodeKind::Resource, {});
        const auto evidence = add_node(evidence_id,
                                       "Event " + std::to_string(i) +
                                       " / line " + std::to_string(event.line),
                                       GraphNodeKind::Evidence, i);
        out.edges.push_back({task, resource, event.type, i,
                             event.line, event.timestamp_us});
        out.edges.push_back({evidence, task, "supports", i,
                             event.line, event.timestamp_us});
        out.edges.push_back({evidence, resource, "supports", i,
                             event.line, event.timestamp_us});
    }
    if (events.size() > scan_count) out.truncated = true;
    if (!out.evidence_complete)
        out.warnings.emplace_back("Trace sequence/loss integrity is incomplete or unverified; no unobserved state is inferred");
    if (out.truncated)
        out.warnings.emplace_back("Graph display is capped; omitted relations remain unknown");
    if (out.nodes.empty())
        out.warnings.emplace_back("No matching observed resource events in the selected window");
    return out;
}
} // namespace observatory
