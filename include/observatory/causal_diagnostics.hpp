// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace observatory {

// W11: all findings are OBSERVED PATTERNS or CANDIDATES. Never claim that
// trace-based correlation establishes a scheduler bug or a deadlock in hardware.
struct CausalFinding {
    std::string code;
    std::string confidence; // "observed-pattern", "candidate", "unverified"
    std::string task;
    std::string resource;
    std::string related_task;
    std::string explanation;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
    std::uint64_t observed_us{};
    std::vector<std::size_t> evidence_events; // decoded indices, in evidence order
};
struct CausalLimits {
    std::size_t max_events{100000};
    std::size_t max_tasks{2000};
    std::size_t max_findings{1000};
    std::size_t max_intervals{20000};
};
struct CausalView {
    std::string clock_domain;
    bool evidence_complete{false};
    bool limited{false};
    std::size_t scanned_events{};
    std::vector<CausalFinding> findings;
    std::vector<std::string> warnings;
};

inline CausalView analyze_causal_patterns(const DecodedTrace& trace, const Report& report,
                                          std::uint64_t long_wait_threshold_us = 0,
                                          CausalLimits limits = {}) {
    if (!limits.max_events || !limits.max_tasks || !limits.max_findings ||
        !limits.max_intervals)
        throw Error("W11 causal analysis limits must be positive");
    CausalView result;
    result.clock_domain = trace.identity.clock.empty() ? "unspecified" : trace.identity.clock;
    result.evidence_complete = report.complete && trace.evidence.sequence_available &&
        trace.evidence.missing_events == 0 && trace.evidence.declared_drops == 0;
    const auto& events = trace.capture.events;
    const auto cap = std::min(events.size(), limits.max_events);
    struct Owner { std::string task; std::size_t acquired{}; };
    struct Waiting {
        std::string resource;
        std::uint64_t since{};
        std::size_t begun{};
        std::string owner_task;
        std::optional<std::size_t> owner_acquire;
        std::optional<std::size_t> owner_release;
    };
    struct Priority { std::uint64_t time{}; int number{}; std::size_t index{}; };
    std::map<std::string,Owner> held;
    std::map<std::string,Waiting> waiting;
    std::map<std::string,std::vector<Priority>> priority;
    using EventKey = std::tuple<std::string,std::string,std::string,std::uint64_t>;
    std::map<EventKey,std::size_t> source;
    for (std::size_t i = 0; i < cap; ++i) {
        const auto& e = events[i];
        if (e.type == "SWITCH_IN" || e.type == "SWITCH_OUT" ||
            e.type == "TASK_RELEASE" || e.type == "TASK_BEGIN")
            source.try_emplace(EventKey{e.type,e.task,e.object,e.timestamp_us},i);
    }
    auto at = [&](const std::string& kind, const std::string& task,
                  const std::string& object, std::uint64_t time)
        -> std::optional<std::size_t> {
        auto it = source.find({kind,task,object,time});
        return it == source.end() ? std::nullopt{} : std::optional<std::size_t>(it->second);
    };
    auto urgency = [&](const std::string& task, std::uint64_t time)
        -> std::optional<Priority> {
        const auto it = priority.find(task);
        if (it == priority.end()) return {};
        std::optional<Priority> latest;
        for (const auto& p : it->second) {
            if (p.time > time) break;
            latest = p;
        }
        return latest;
    };
    auto changed = [&](const std::string& task, std::uint64_t from,
                       std::uint64_t to) {
        const auto it = priority.find(task);
        if (it == priority.end()) return false;
        return std::any_of(it->second.begin(),it->second.end(),
            [&](const Priority& p) { return p.time > from && p.time <= to; });
    };
    auto add = [&](CausalFinding f) {
        if (result.findings.size() < limits.max_findings) {
            std::sort(f.evidence_events.begin(),f.evidence_events.end());
            f.evidence_events.erase(
                std::unique(f.evidence_events.begin(),f.evidence_events.end()),
                f.evidence_events.end());
            if (!result.evidence_complete) f.confidence = "unverified";
            result.findings.push_back(std::move(f));
        } else result.limited = true;
    };
    auto push = [](std::vector<std::size_t>& ids,
                   std::optional<std::size_t> n) {
        if (n) ids.push_back(*n);
    };
    for (std::size_t i = 0; i < cap; ++i) {
        ++result.scanned_events;
        const auto& e = events[i];
        if (e.type == "DROP" || (e.type == "SESSION" && i != 0)) {
            result.evidence_complete = false;
            held.clear();
            waiting.clear();
            priority.clear();
            continue;
        }
        if (e.type == "CREATE" || e.type == "PRIORITY") {
            if (e.task != "-" && e.value >= 0 && e.value <= 255)
                priority[e.task].push_back({e.timestamp_us,static_cast<int>(e.value),i});
            continue;
        }
        if (e.object.empty() || e.object == "-" || e.task.empty() ||
            e.task == "-") continue;
        if (e.type == "MUTEX_WAIT") {
            if (waiting.size() >= limits.max_tasks && !waiting.contains(e.task)) {
                result.limited = true;
                break;
            }
            if (waiting.contains(e.task)) {
                add({"AMBIGUOUS_WAIT_SEQUENCE","candidate",e.task,e.object,{},
                     "Task already has an unpaired mutex wait; recorded state is ambiguous",
                     e.timestamp_us,e.timestamp_us,0,{waiting[e.task].begun,i}});
                waiting.erase(e.task);
                continue;
            }
            Waiting w;
            w.resource = e.object;
            w.since = e.timestamp_us;
            w.begun = i;
            if (auto owner = held.find(e.object);
                owner != held.end() && owner->second.task != e.task) {
                w.owner_task = owner->second.task;
                w.owner_acquire = owner->second.acquired;
            }
            waiting.emplace(e.task,w);
            // Follow only explicit task->waited-mutex->recorded-owner edges.
            // The resulting cycle is a snapshot candidate, not a proven deadlock.
            std::set<std::string> seen{e.task};
            std::vector<std::size_t> proof{i};
            push(proof,w.owner_acquire);
            std::string current = w.owner_task;
            for (std::size_t depth=0; depth < limits.max_tasks && !current.empty(); ++depth) {
                if (current == e.task) {
                    add({"CIRCULAR_MUTEX_WAIT_CANDIDATE","candidate",e.task,e.object,current,
                         "Observed mutex ownership and waits form a closed cycle; "
                         "kernel deadlock/progress are not proven",
                         e.timestamp_us,e.timestamp_us,0,proof});
                    break;
                }
                if (!seen.insert(current).second) break;
                const auto other = waiting.find(current);
                if (other == waiting.end()) break;
                proof.push_back(other->second.begun);
                const auto owner = held.find(other->second.resource);
                if (owner == held.end() || owner->second.task == current) break;
                proof.push_back(owner->second.acquired);
                current = owner->second.task;
            }
        } else if (e.type == "MUTEX_UNLOCK") {
            auto owner = held.find(e.object);
            if (owner == held.end() || owner->second.task != e.task) {
                add({"UNMATCHED_MUTEX_UNLOCK","candidate",e.task,e.object,{},
                     "Owner not proven for this unlock; later ownership is unknown",
                     e.timestamp_us,e.timestamp_us,0,{i}});
                held.erase(e.object);
                continue;
            }
            for (auto& [task,w] : waiting) {
                (void)task;
                if (w.resource == e.object && w.owner_task == e.task &&
                    w.owner_acquire && *w.owner_acquire == owner->second.acquired)
                    w.owner_release = i;
            }
            held.erase(owner);
        } else if (e.type == "MUTEX_ACQUIRE") {
            const auto it = waiting.find(e.task);
            if (it != waiting.end() && it->second.resource == e.object) {
                const auto w = it->second;
                waiting.erase(it);
                if (e.timestamp_us >= w.since) {
                    const auto duration = e.timestamp_us - w.since;
                    if (long_wait_threshold_us && duration >= long_wait_threshold_us) {
                        add({"LONG_COMPLETED_MUTEX_WAIT","candidate",e.task,e.object,
                             w.owner_task,"Wait exceeded operator-specified threshold; "
                             "starvation or scheduling fault is not proven",
                             w.since,e.timestamp_us,duration,{w.begun,i}});
                    }
                    // Detect a measured medium-urgency execution inside a
                    // high-urgency wait on an explicitly lower-urgency owner.
                    if (w.owner_acquire && w.owner_release) {
                        const auto high = urgency(e.task,w.since);
                        const auto low = urgency(w.owner_task,w.since);
                        const auto released = events[*w.owner_release].timestamp_us;
                        if (high && low && high->number > low->number &&
                            released >= w.since && released <= e.timestamp_us &&
                            !changed(e.task,w.since,e.timestamp_us) &&
                            !changed(w.owner_task,w.since,e.timestamp_us)) {
                            std::size_t inspected=0;
                            for (const auto& span : report.intervals) {
                                if (++inspected > limits.max_intervals) {
                                    result.limited = true; break;
                                }
                                if (span.state != "running" || span.task == e.task ||
                                    span.task == w.owner_task || span.end_us <= w.since ||
                                    span.start_us >= released) continue;
                                const auto middle = urgency(span.task,span.start_us);
                                if (!middle || !(low->number < middle->number &&
                                                 middle->number < high->number) ||
                                    changed(span.task,w.since,released)) continue;
                                auto start_ev = at("SWITCH_IN",span.task,span.core,span.start_us);
                                auto end_ev = at("SWITCH_OUT",span.task,span.core,span.end_us);
                                if (!start_ev && !end_ev) continue; // no source to cite
                                const auto overlap_begin = std::max(span.start_us,w.since);
                                const auto overlap_end = std::min(span.end_us,released);
                                if (overlap_end <= overlap_begin) continue;
                                std::vector<std::size_t> proof{w.begun,i,*w.owner_acquire,
                                                               *w.owner_release,
                                                               high->index,low->index,middle->index};
                                push(proof,start_ev);
                                push(proof,end_ev);
                                add({"PRIORITY_INVERSION_EXECUTION_PATTERN","observed-pattern",
                                     e.task,e.object,w.owner_task,
                                     "Higher-priority task waited for lower-priority recorded owner "
                                     "while a medium-priority task executed. Causation, same-core "
                                     "scheduling impact and priority inheritance not proven",
                                     overlap_begin,overlap_end,overlap_end-overlap_begin,proof});
                                break;
                            }
                        }
                    }
                    // Explicit mutex wait inside release->application TASK_BEGIN
                    // is linked, not claimed as the source of task clock drift.
                    for (const auto& c : report.cycles) {
                        if (c.task != e.task || !c.start_us ||
                            c.release_us > w.since || *c.start_us < e.timestamp_us) continue;
                        const auto release = at("TASK_RELEASE",e.task,"-",c.release_us);
                        const auto begin = at("TASK_BEGIN",e.task,"-",*c.start_us);
                        if (!release || !begin) continue;
                        std::vector<std::size_t> proof{*release,*begin,w.begun,i};
                        push(proof,w.owner_acquire);
                        push(proof,w.owner_release);
                        add({"MUTEX_WAIT_IN_APPLICATION_BEGIN_DELAY","observed-pattern",
                             e.task,e.object,w.owner_task,
                             "Mutex wait overlaps release-to-TASK_BEGIN interval; "
                             "application delay is observed, root cause and clock drift unproven",
                             c.release_us,*c.start_us,*c.start_us-c.release_us,proof});
                        break;
                    }
                }
            }
            if (held.contains(e.object)) {
                add({"CONFLICTING_MUTEX_ACQUIRE","candidate",e.task,e.object,
                     held[e.object].task,
                     "Acquire recorded while a different or recursive owner is still recorded; "
                     "owner semantics must be resolved by a versioned RTOS adapter",
                     e.timestamp_us,e.timestamp_us,0,{held[e.object].acquired,i}});
                held.erase(e.object); // deliberately unknown rather than replacing
            } else {
                held[e.object] = {e.task,i};
            }
        }
    }
    if (events.size() > cap) result.limited = true;
    if (long_wait_threshold_us && !result.limited && result.evidence_complete &&
        !events.empty()) {
        const auto observed_end = events[result.scanned_events-1].timestamp_us;
        for (const auto& [task,w] : waiting) {
            if (observed_end < w.since ||
                observed_end - w.since < long_wait_threshold_us) continue;
            std::vector<std::size_t> proof{w.begun,result.scanned_events-1};
            push(proof,w.owner_acquire);
            add({"LONG_UNCLOSED_MUTEX_WAIT","candidate",task,w.resource,w.owner_task,
                 "Wait remains unpaired in the available capture; exceeding a user "
                 "threshold does NOT establish starvation or a live deadlock",
                 w.since,observed_end,observed_end-w.since,proof});
        }
    }
    if (!result.evidence_complete) {
        for (auto& f : result.findings) f.confidence = "unverified";
        result.warnings.push_back("Sequence/loss/analyzer evidence incomplete; findings are not qualified");
    }
    if (result.limited)
        result.warnings.push_back("Bounded scan or findings cap reached; unseen relations remain unknown");
    if (!long_wait_threshold_us)
        result.warnings.push_back("No starvation-duration threshold configured; prolonged waits not classified");
    result.warnings.push_back("Cycles are potential deadlocks; priority inversion observations do not prove causation");
    return result;
}
} // namespace observatory
