// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace observatory {

// W08 snapshots are replayed observations, not live TCB/queue state.
enum class ReplayResourceKind { Unknown, Acquired, Released };
struct ReplayResource {
    std::string id;
    ReplayResourceKind kind{ReplayResourceKind::Unknown};
    std::string last_owner;
    std::size_t event_index{};
    std::size_t source_line{};
    std::uint64_t timestamp_us{};
};
struct ReplayWait {
    std::string task;
    std::string resource;
    bool active{false};
    bool known{false};
    std::size_t event_index{};
    std::size_t source_line{};
    std::uint64_t timestamp_us{};
};
struct ReplaySnapshot {
    std::string clock_domain;
    std::size_t cursor{};
    std::size_t applied_events{};
    std::uint64_t timestamp_us{};
    bool integrity_verified{false};
    bool capped{false};
    std::vector<ReplayResource> resources;
    std::vector<ReplayWait> waits;
    std::vector<std::string> warnings;
};

// Replay through event-index (inclusive), preserving order for equal timestamps.
// max_events is a hard CPU budget; a snapshot beyond it is explicitly incomplete.
inline ReplaySnapshot replay_at(const DecodedTrace& trace, std::size_t event_index,
                                std::size_t max_events = 100000) {
    if (!max_events) throw Error("Replay event budget must be positive");
    const auto& events = trace.capture.events;
    if (!events.empty() && event_index >= events.size())
        throw Error("Replay event index exceeds capture bounds");
    ReplaySnapshot out;
    out.clock_domain = trace.identity.clock.empty() ? "unspecified" : trace.identity.clock;
    out.cursor = event_index;
    out.integrity_verified = trace.evidence.sequence_available &&
                             trace.evidence.missing_events == 0 &&
                             trace.evidence.declared_drops == 0;
    std::map<std::string, ReplayResource> resources;
    std::map<std::pair<std::string, std::string>, ReplayWait> waits;
    const auto to_apply = events.empty() ? 0 : std::min(event_index + 1, max_events);
    out.capped = !events.empty() && event_index + 1 > max_events;
    bool inversion = false;
    bool invalid_unlock = false;
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < to_apply; ++i) {
        const auto& e = events[i];
        if (i != 0 && e.timestamp_us < previous) {
            inversion = true;
            out.integrity_verified = false;
        }
        previous = e.timestamp_us;
        if (e.type == "SESSION" || e.type == "DROP") {
            if (e.type == "DROP") out.integrity_verified = false;
            resources.clear();
            waits.clear();
            continue; // continuity across an epoch/loss boundary is not presumed
        }
        if (e.task.empty() || e.task == "-" ||
            e.object.empty() || e.object == "-") continue;
        const bool mutex = e.type == "MUTEX_ACQUIRE" ||
                           e.type == "MUTEX_UNLOCK" ||
                           e.type == "MUTEX_WAIT";
        const bool generic = e.type == "WAIT_BEGIN" || e.type == "WAIT_END";
        if (!mutex && !generic) continue;
        const auto key = std::make_pair(e.task, e.object);
        auto update_wait = [&](bool active, bool known) {
            auto& w = waits[key];
            w = {e.task, e.object, active, known, i, e.line, e.timestamp_us};
        };
        if (e.type == "MUTEX_WAIT" || e.type == "WAIT_BEGIN") {
            update_wait(true, true);
        } else if (e.type == "WAIT_END") {
            const auto it = waits.find(key);
            update_wait(false, it != waits.end() && it->second.known &&
                               it->second.active);
        } else if (e.type == "MUTEX_ACQUIRE") {
            auto& r = resources[e.object];
            const bool conflict = r.kind == ReplayResourceKind::Acquired &&
                                  r.last_owner != e.task;
            if (conflict) {
                inversion = true; // inconsistent evidence, NOT a proven transfer
                out.integrity_verified = false;
                r = {e.object, ReplayResourceKind::Unknown, {}, i, e.line, e.timestamp_us};
            } else {
                r = {e.object, ReplayResourceKind::Acquired, e.task, i, e.line, e.timestamp_us};
            }
            update_wait(false, true); // explicit acquire ends that task's mutex wait
        } else if (e.type == "MUTEX_UNLOCK") {
            auto& r = resources[e.object];
            if (r.kind == ReplayResourceKind::Acquired && r.last_owner == e.task)
                r = {e.object, ReplayResourceKind::Released, e.task, i, e.line, e.timestamp_us};
            else {
                invalid_unlock = true;
                out.integrity_verified = false;
                r = {e.object, ReplayResourceKind::Unknown, {}, i, e.line, e.timestamp_us};
            }
        }
    }
    out.applied_events = to_apply;
    if (to_apply) out.timestamp_us = events[to_apply - 1].timestamp_us;
    for (auto& [_, resource] : resources) out.resources.push_back(std::move(resource));
    for (auto& [_, wait] : waits) out.waits.push_back(std::move(wait));
    if (!out.integrity_verified)
        out.warnings.emplace_back("Incomplete/unverified trace: displayed states are last recorded observations, NOT proven live states");
    if (out.capped)
        out.warnings.emplace_back("Replay budget reached: events after the limit have NOT been applied");
    if (inversion)
        out.warnings.emplace_back("Event timestamp/order or ownership conflict: affected state is unknown");
    if (invalid_unlock)
        out.warnings.emplace_back("Unmatched mutex unlock: owner remains unknown");
    if (events.empty())
        out.warnings.emplace_back("Empty capture: no state can be replayed");
    return out;
}
} // namespace observatory
