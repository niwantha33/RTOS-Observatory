// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "observatory/session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace observatory {

// Offline normalized-v1 observations. This is NOT a vendor kernel-object model.
enum class ResourceKind {
    Mutex, Semaphore, Queue, Gate, Mailbox, Event, Notification, Generic
};
inline const char* resource_kind_name(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::Mutex: return "Mutex";
        case ResourceKind::Semaphore: return "Semaphore";
        case ResourceKind::Queue: return "Queue";
        case ResourceKind::Gate: return "Gate";
        case ResourceKind::Mailbox: return "Mailbox";
        case ResourceKind::Event: return "Event";
        case ResourceKind::Notification: return "Notification";
        case ResourceKind::Generic: return "Generic/unknown";
    }
    return "Generic/unknown";
}
inline ResourceKind observed_resource_kind(const std::string& name, bool mutex_event) {
    if (mutex_event || name.starts_with("mutex:")) return ResourceKind::Mutex;
    if (name.starts_with("sem:")) return ResourceKind::Semaphore;
    if (name.starts_with("queue:")) return ResourceKind::Queue;
    if (name.starts_with("gate:")) return ResourceKind::Gate;
    if (name.starts_with("mailbox:")) return ResourceKind::Mailbox;
    if (name.starts_with("event:")) return ResourceKind::Event;
    if (name.starts_with("notify:")) return ResourceKind::Notification;
    return ResourceKind::Generic;
}
struct ResourceLimits {
    std::size_t max_events_scanned{100000};
    std::size_t max_resources{1000};
    std::size_t max_pending_waits{10000};
    std::size_t max_display_events{20000};
    std::size_t max_findings{2000};
};
struct ResourceSummary {
    std::string name;
    ResourceKind kind{ResourceKind::Generic};
    std::size_t observed_events{};
    std::size_t observed_wait_starts{};
    std::size_t completed_waits{};
    std::size_t unresolved_waits{};
    std::optional<std::uint64_t> max_completed_wait_us;
    std::optional<std::string> last_recorded_owner; // mutex only: NOT verified current owner
    bool owner_observation_valid{false};
    bool continuity_lost{false};
};
struct ResourceObservation {
    std::string resource;
    std::string task;
    std::string type; // literal original normalized event type
    std::size_t event_index{};
    std::size_t source_line{};
    std::uint64_t timestamp_us{};
    std::optional<std::uint64_t> completed_wait_us;
    std::optional<std::size_t> wait_start_event;
    bool evidence_verified{false};
};
struct ResourceAnomaly {
    std::string code;
    std::string resource;
    std::string task;
    std::size_t event_index{};
    std::size_t source_line{};
    std::string explanation;
};
struct ResourceView {
    std::string source_label;
    std::string rtos_label;
    std::string clock_domain;
    bool evidence_verified{false};
    bool limited{false};
    std::size_t examined_events{};
    std::vector<ResourceSummary> resources;
    std::vector<ResourceObservation> observations;
    std::vector<ResourceAnomaly> anomalies;
    std::vector<std::string> warnings;
};

// No queue depth, semaphore count, gate admission, mailbox occupancy, event
// bits, or notification value is derived from generic WAIT_BEGIN/WAIT_END.
inline ResourceView analyze_resource_observations(const DecodedTrace& trace,
                                                   bool report_complete = true,
                                                   ResourceLimits limits = {}) {
    if (limits.max_events_scanned == 0 || limits.max_resources == 0 ||
        limits.max_pending_waits == 0 || limits.max_display_events == 0 ||
        limits.max_findings == 0)
        throw Error("Resource-analysis limits must be positive");
    ResourceView out;
    out.source_label = trace.capture.source;
    out.rtos_label = trace.identity.rtos.empty() ? "unspecified" : trace.identity.rtos;
    out.clock_domain = trace.identity.clock.empty() ? "unspecified" : trace.identity.clock;
    out.evidence_verified = report_complete && trace.evidence.sequence_available &&
                            trace.evidence.missing_events == 0 &&
                            trace.evidence.declared_drops == 0;
    struct Pending {
        std::size_t index{};
        std::uint64_t timestamp_us{};
        bool mutex{};
    };
    using Key = std::pair<std::string, std::string>; // (task, resource)
    std::map<std::string, ResourceSummary> summaries;
    std::map<Key, Pending> pending;
    bool loss_seen = false;
    auto issue = [&](const std::string& code, const Event& e,
                     std::size_t index, const std::string& message) {
        if (out.anomalies.size() < limits.max_findings)
            out.anomalies.push_back({code, e.object, e.task, index, e.line, message});
        else out.limited = true;
    };
    const auto& events = trace.capture.events;
    const auto scan_count = std::min(events.size(), limits.max_events_scanned);
    for (std::size_t i = 0; i < scan_count; ++i) {
        ++out.examined_events;
        const auto& e = events[i];
        if (e.type == "DROP" || (e.type == "SESSION" && i != 0)) {
            loss_seen = true;
            out.evidence_verified = false;
            // Following discontinuity, previously observed ownership and waits
            // cannot be assumed valid. Preserve resource rows marked unknown.
            for (auto& [name, r] : summaries) {
                (void)name;
                r.owner_observation_valid = false;
                r.last_recorded_owner.reset();
                r.continuity_lost = true;
            }
            for (const auto& [key, wait] : pending) {
                (void)wait;
                if (auto r = summaries.find(key.second); r != summaries.end())
                    ++r->second.unresolved_waits;
            }
            pending.clear();
            continue;
        }
        const bool mutex_event = e.type == "MUTEX_WAIT" ||
                                 e.type == "MUTEX_ACQUIRE" ||
                                 e.type == "MUTEX_UNLOCK";
        const bool generic_wait = e.type == "WAIT_BEGIN" || e.type == "WAIT_END";
        if (!mutex_event && !generic_wait) continue;
        if (e.object.empty() || e.object == "-" || e.task.empty() || e.task == "-") {
            issue("INVALID_RESOURCE_EVENT", e, i,
                  "The normalized row lacks a task or resource identity");
            continue;
        }
        auto r_it = summaries.find(e.object);
        if (r_it == summaries.end()) {
            if (summaries.size() >= limits.max_resources) {
                out.limited = true;
                break;
            }
            ResourceSummary created;
            created.name = e.object;
            created.kind = observed_resource_kind(e.object, mutex_event);
            r_it = summaries.emplace(e.object, std::move(created)).first;
        }
        auto& r = r_it->second;
        if (mutex_event && r.kind != ResourceKind::Mutex) {
            // Conflicting object categories in one epoch are ambiguous.
            issue("CONFLICTING_RESOURCE_TYPE", e, i,
                  "A mutex event conflicts with a previously classified resource");
            r.continuity_lost = true;
            r.owner_observation_valid = false;
            r.last_recorded_owner.reset();
            out.evidence_verified = false;
            continue;
        }
        ++r.observed_events;
        ResourceObservation obs;
        obs.resource = e.object;
        obs.task = e.task;
        obs.type = e.type;
        obs.event_index = i;
        obs.source_line = e.line;
        obs.timestamp_us = e.timestamp_us;
        obs.evidence_verified = out.evidence_verified && !loss_seen;
        const Key key{e.task, e.object};
        if (e.type == "MUTEX_WAIT" || e.type == "WAIT_BEGIN") {
            ++r.observed_wait_starts;
            if (const auto prior = pending.find(key); prior != pending.end()) {
                ++r.unresolved_waits;
                issue("DUPLICATE_WAIT_BEGIN",e,i,
                      "Repeated wait start; duration of prior wait is unknown");
                pending.erase(prior);
            }
            if (pending.size() >= limits.max_pending_waits) {
                ++r.unresolved_waits;
                out.limited = true;
                // Stop on cap; otherwise later WAIT_END would be misclassified.
                break;
            }
            pending.emplace(key, Pending{i, e.timestamp_us, e.type == "MUTEX_WAIT"});
        } else if (e.type == "WAIT_END" || e.type == "MUTEX_ACQUIRE") {
            const auto it = pending.find(key);
            const bool matching = it != pending.end() &&
                it->second.mutex == (e.type == "MUTEX_ACQUIRE");
            if (matching) {
                if (e.timestamp_us >= it->second.timestamp_us) {
                    const auto length = e.timestamp_us - it->second.timestamp_us;
                    ++r.completed_waits;
                    if (!r.max_completed_wait_us || length > *r.max_completed_wait_us)
                        r.max_completed_wait_us = length;
                    obs.completed_wait_us = length;
                    obs.wait_start_event = it->second.index;
                } else {
                    ++r.unresolved_waits;
                    issue("BACKWARDS_WAIT_INTERVAL",e,i,
                          "Completed wait endpoint precedes its start");
                    out.evidence_verified = false;
                }
                pending.erase(it);
            } else if (e.type == "WAIT_END") {
                issue("UNPAIRED_WAIT_END",e,i,
                      "No matching generic wait start: a wakeup cause is unknown");
            }
            if (e.type == "MUTEX_ACQUIRE") {
                if (r.owner_observation_valid) {
                    issue("AMBIGUOUS_MUTEX_ACQUIRE",e,i,
                          "Acquire while a prior recorded owner is still active; "
                          "recursive/ownership semantics require an RTOS-specific adapter");
                    r.last_recorded_owner.reset();
                    r.owner_observation_valid = false;
                    r.continuity_lost = true;
                } else {
                    r.last_recorded_owner = e.task;
                    r.owner_observation_valid = true;
                }
            }
        } else if (e.type == "MUTEX_UNLOCK") {
            if (!r.owner_observation_valid || !r.last_recorded_owner ||
                *r.last_recorded_owner != e.task) {
                issue("UNMATCHED_MUTEX_UNLOCK",e,i,
                      "No matching recorded mutex owner; current ownership is unknown");
                r.continuity_lost = true;
            }
            r.last_recorded_owner.reset();
            r.owner_observation_valid = false;
        }
        if (out.observations.size() < limits.max_display_events)
            out.observations.push_back(std::move(obs));
        else out.limited = true;
    }
    if (events.size() > scan_count) out.limited = true;
    for (const auto& [key, wait] : pending) {
        (void)wait;
        if (auto r = summaries.find(key.second); r != summaries.end())
            ++r->second.unresolved_waits;
    }
    for (auto& [name, r] : summaries) {
        (void)name;
        // Even a final observed acquire is not attestation of live ownership.
        if (!out.evidence_verified) r.continuity_lost = true;
        out.resources.push_back(std::move(r));
    }
    if (!out.evidence_verified)
        out.warnings.emplace_back("Incomplete/unverified sequence or analyzer: resource state between events is unknown");
    if (loss_seen)
        out.warnings.emplace_back("Trace loss/epoch change invalidated prior mutex ownership and pending waits");
    if (out.limited)
        out.warnings.emplace_back("Resource analysis bounded: some events or relationships may be omitted");
    out.warnings.emplace_back("Generic waits do not reveal semaphore count, gate admission, queue depth, "
                              "mailbox occupancy, event bits or notification values");
    out.warnings.emplace_back("RTOS label is unverified: native object semantics require versioned adapters");
    return out;
}

} // namespace observatory
