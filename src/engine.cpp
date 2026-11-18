// SPDX-License-Identifier: Apache-2.0
#include "observatory/engine.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace observatory {
namespace {
constexpr std::size_t kMaxLine = 4096;
constexpr std::uint64_t kMaxTimestamp = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
const std::set<std::string> known_types = {
    "SESSION", "CREATE", "PRIORITY", "PERIOD", "SWITCH_IN", "SWITCH_OUT",
    "ISR_ENTER", "ISR_EXIT", "MUTEX_WAIT", "MUTEX_ACQUIRE", "MUTEX_UNLOCK",
    "WAIT_BEGIN", "WAIT_END", "TASK_RELEASE", "TASK_BEGIN", "TASK_FINISH", "DROP"
};
std::string json_quote(std::string_view text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
std::array<std::string,5> split_line(const std::string& line, std::size_t number) {
    std::array<std::string,5> cells;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto end = line.find('\t', begin);
        if (i < cells.size()-1 && end == std::string::npos) throw Error("line " + std::to_string(number) + ": expected 5 tab-separated fields");
        if (i == cells.size()-1 && end != std::string::npos) throw Error("line " + std::to_string(number) + ": too many fields");
        cells[i] = line.substr(begin, end == std::string::npos ? end : end - begin);
        begin = end == std::string::npos ? line.size() : end + 1;
    }
    return cells;
}
template<class Int>
Int number(const std::string& s, std::size_t line) {
    Int value{};
    if (s.empty()) throw Error("line " + std::to_string(line) + ": empty number");
    const auto [end, error] = std::from_chars(s.data(), s.data()+s.size(), value);
    if (error != std::errc{} || end != s.data()+s.size()) throw Error("line " + std::to_string(line) + ": invalid number " + s);
    return value;
}
void check_name(const std::string& s, std::size_t line) {
    if (s.empty() || s.size() > 64) throw Error("line " + std::to_string(line) + ": invalid identifier length");
    for (unsigned char c : s) {
        if (c < 33 || c > 126 || c == '"' || c == '\\' || c == '<' || c == '>')
            throw Error("line " + std::to_string(line) + ": invalid identifier character");
    }
}
struct Core {
    std::string active;
    std::string paused;
    std::uint64_t since{};
    std::uint64_t isr_since{};
    unsigned isr_depth{};
};
struct Lock { std::string owner; std::uint64_t acquired{}; };
struct PendingWait { std::uint64_t since{}; std::string owner; bool valid_owner{false}; std::string kind; std::optional<std::uint64_t> owner_release_us; };
struct PriorityRecord { std::uint64_t timestamp{}; int priority{}; };
std::optional<int> priority_at(const std::map<std::string,std::vector<PriorityRecord>>& priorities,
                               const std::string& task, std::uint64_t time) {
    const auto it = priorities.find(task);
    if (it == priorities.end()) return std::nullopt;
    std::optional<int> result;
    for (const auto& value : it->second) {
        if (value.timestamp > time) break;
        result = value.priority;
    }
    return result;
}
std::optional<std::int64_t> safe_diff(std::uint64_t a, std::uint64_t b) {
    if (a >= b) {
        const auto diff = a-b;
        if (diff > kMaxTimestamp) return std::nullopt;
        return static_cast<std::int64_t>(diff);
    }
    const auto diff = b-a;
    if (diff > kMaxTimestamp) return std::nullopt;
    return -static_cast<std::int64_t>(diff);
}
std::string resource_kind(const std::string& resource) {
    if (resource.starts_with("sem:")) return "semaphore";
    if (resource.starts_with("queue:")) return "queue";
    if (resource.starts_with("event:")) return "event";
    if (resource.starts_with("notify:")) return "notification";
    return "generic";
}
void append_interval(Report& report, const std::string& task, const std::string& core,
                     const std::string& state, std::uint64_t begin, std::uint64_t end) {
    if (end > begin) report.intervals.push_back({task, core, state, begin, end});
}
} // namespace

Capture parse_trace(std::istream& input, std::size_t max_events) {
    Capture result;
    std::string line;
    std::size_t line_number = 0;
    bool seen_session = false;
    std::uint64_t previous_timestamp = 0;
    while (true) {
        line.clear();
        char ch;
        while (input.get(ch) && ch != '\n') {
            if (line.size() >= kMaxLine) throw Error("line exceeds 4096 bytes");
            line.push_back(ch);
        }
        if (!input && !input.eof()) throw Error("trace read failed");
        if (line.empty() && input.eof()) break;
        ++line_number;
        if (line.size() > kMaxLine) throw Error("line " + std::to_string(line_number) + ": line exceeds 4096 bytes");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto cells = split_line(line, line_number);
        auto timestamp = number<std::uint64_t>(cells[0], line_number);
        if (timestamp > kMaxTimestamp || timestamp < previous_timestamp) throw Error("line " + std::to_string(line_number) + ": invalid or decreasing timestamp");
        if (!known_types.contains(cells[1])) throw Error("line " + std::to_string(line_number) + ": unsupported event " + cells[1]);
        check_name(cells[2], line_number);
        check_name(cells[3], line_number);
        const auto value = number<std::int64_t>(cells[4], line_number);
        if (!seen_session) {
            if (cells[1] != "SESSION" || timestamp != 0 || cells[2] != "-") throw Error("first event must be SESSION at timestamp 0");
            result.source = cells[3];
            result.session = cells[3];
            seen_session = true;
        } else if (cells[1] == "SESSION") {
            throw Error("multiple SESSION events not permitted; separate reset epochs");
        }
        if ((cells[1] == "CREATE" || cells[1] == "PRIORITY") && (value < 0 || value > 255))
            throw Error("line " + std::to_string(line_number) + ": priority out of range 0..255");
        if (cells[1] == "PERIOD" && value <= 0)
            throw Error("line " + std::to_string(line_number) + ": period must be positive");
        if (cells[1] == "DROP" && value <= 0)
            throw Error("line " + std::to_string(line_number) + ": drop count must be positive");
        if (result.events.size() >= max_events) throw Error("capture exceeds max_events");
        result.events.push_back({timestamp, cells[1], cells[2], cells[3], value, line_number});
        previous_timestamp = timestamp;
    }
    if (!seen_session || result.events.size() < 2) throw Error("missing SESSION or no trace events");
    return result;
}

Report analyze(const Capture& capture) {
    if (capture.events.empty() || capture.events.front().type != "SESSION") throw Error("invalid capture session");
    Report report;
    report.source = capture.source;
    report.session = capture.session;
    report.start_us = capture.events.front().timestamp_us;
    report.end_us = capture.events.back().timestamp_us;
    std::map<std::string,Thread> tasks;
    std::map<std::string,std::vector<PriorityRecord>> priorities;
    std::map<std::string,Core> cores;
    std::map<std::string,Lock> locks;
    std::map<std::pair<std::string,std::string>,PendingWait> pending;
    std::map<std::string,std::size_t> release_count;
    std::map<std::string,std::uint64_t> first_release;
    bool interrupted = false;
    auto warn = [&](const std::string& what) { report.warnings.push_back(what); };
    auto state_error = [&](const std::string& what) { report.complete = false; warn(what); };

    for (const auto& e : capture.events) {
        const std::string context = "line " + std::to_string(e.line) + ": ";
        if (e.type == "SESSION") continue;
        if (e.type == "DROP") {
            interrupted = true;
            state_error(context + "reported lost trace events: " + std::to_string(e.value));
            // Following loss, ownership and scheduler context are untrustworthy.
            locks.clear(); pending.clear(); cores.clear();
            continue;
        }
        if (e.type == "CREATE") {
            if (e.task == "-" || tasks.contains(e.task)) { state_error(context + "invalid/reused task identity"); continue; }
            tasks[e.task] = {e.task, static_cast<int>(e.value), std::nullopt, e.object == "idle"};
            priorities[e.task].push_back({e.timestamp_us, static_cast<int>(e.value)});
            continue;
        }
        if (e.type == "PRIORITY") {
            if (!tasks.contains(e.task)) { state_error(context + "priority for unknown task"); continue; }
            tasks[e.task].priority = static_cast<int>(e.value);
            priorities[e.task].push_back({e.timestamp_us, static_cast<int>(e.value)});
            continue;
        }
        if (e.type == "PERIOD") {
            if (!tasks.contains(e.task)) { state_error(context + "period for unknown task"); continue; }
            tasks[e.task].period_us = static_cast<std::uint64_t>(e.value);
            continue;
        }
        if (e.type != "ISR_ENTER" && e.type != "ISR_EXIT" && !tasks.contains(e.task)) {
            state_error(context + "event for unknown task " + e.task);
            continue;
        }
        if (e.type == "SWITCH_IN" || e.type == "SWITCH_OUT" || e.type == "ISR_ENTER" || e.type == "ISR_EXIT") {
            if (e.object == "-") { state_error(context + "missing core identity"); continue; }
            auto& c = cores[e.object];
            if (e.type == "SWITCH_IN") {
                if (c.isr_depth != 0 || !c.active.empty()) {
                    state_error(context + "overlapping switch-in or switch-in inside ISR");
                    c.active.clear(); c.paused.clear();
                } else { c.active = e.task; c.since = e.timestamp_us; }
            } else if (e.type == "SWITCH_OUT") {
                if (c.isr_depth != 0 || c.active != e.task || c.active.empty()) {
                    state_error(context + "unpaired switch-out"); c.active.clear(); c.paused.clear();
                } else {
                    append_interval(report, c.active, e.object, tasks[c.active].idle ? "idle" : "running", c.since, e.timestamp_us);
                    c.active.clear();
                }
            } else if (e.type == "ISR_ENTER") {
                if (c.isr_depth == 0) {
                    c.isr_since = e.timestamp_us;
                    if (!c.active.empty()) {
                        append_interval(report, c.active, e.object, tasks[c.active].idle ? "idle" : "running", c.since, e.timestamp_us);
                        c.paused = c.active;
                        c.active.clear();
                    }
                }
                if (c.isr_depth == 128) { state_error(context + "ISR nesting limit exceeded"); }
                else ++c.isr_depth;
            } else {
                if (c.isr_depth == 0) state_error(context + "unpaired ISR exit");
                else if (--c.isr_depth == 0) {
                    append_interval(report, "__ISR__", e.object, "isr", c.isr_since, e.timestamp_us);
                    if (!c.paused.empty()) { c.active = c.paused; c.since = e.timestamp_us; c.paused.clear(); }
                }
            }
            continue;
        }
        if (e.type == "MUTEX_WAIT" || e.type == "WAIT_BEGIN") {
            if (e.object == "-") { state_error(context + "missing resource"); continue; }
            const auto key = std::make_pair(e.task, e.object);
            if (pending.contains(key)) { state_error(context + "duplicate wait begin"); pending.erase(key); continue; }
            const auto owned = locks.find(e.object);
            const bool mutex = e.type == "MUTEX_WAIT";
            const bool known_owner = mutex && owned != locks.end() && owned->second.owner != e.task;
            pending[key] = {e.timestamp_us, known_owner ? owned->second.owner : "", known_owner, mutex ? "mutex" : resource_kind(e.object), std::nullopt};
            continue;
        }
        if (e.type == "MUTEX_ACQUIRE" || e.type == "WAIT_END") {
            const auto key = std::make_pair(e.task, e.object);
            if (auto p = pending.find(key); p != pending.end()) {
                if (p->second.since <= e.timestamp_us)
                    report.waits.push_back({e.task, e.object, p->second.kind, p->second.owner,
                                            p->second.since, e.timestamp_us, p->second.valid_owner, p->second.owner_release_us});
                pending.erase(p);
            } else if (e.type == "WAIT_END") {
                state_error(context + "unpaired wait end");
            }
            if (e.type == "MUTEX_ACQUIRE") {
                if (locks.contains(e.object)) {
                    state_error(context + "mutex acquired while ownership already recorded");
                    locks.erase(e.object); // No unsupported recursive ownership inference.
                } else locks[e.object] = {e.task, e.timestamp_us};
            }
            continue;
        }
        if (e.type == "MUTEX_UNLOCK") {
            const auto owned = locks.find(e.object);
            if (owned == locks.end() || owned->second.owner != e.task) {
                state_error(context + "mutex unlock without proven ownership");
            } else {
                for (auto& [key,w] : pending) {
                    if (key.second == e.object && w.valid_owner && w.owner == e.task && !w.owner_release_us)
                        w.owner_release_us = e.timestamp_us;
                }
                locks.erase(owned);
            }
            continue;
        }
        if (e.type == "TASK_RELEASE") {
            if (!tasks.contains(e.task)) continue;
            if (!first_release.contains(e.task)) first_release[e.task] = e.timestamp_us;
            const auto n = release_count[e.task]++;
            std::optional<std::int64_t> phase;
            const auto period = tasks[e.task].period_us;
            if (period && n <= (kMaxTimestamp - first_release[e.task]) / *period)
                phase = safe_diff(e.timestamp_us, first_release[e.task] + n * *period);
            report.cycles.push_back({e.task, e.timestamp_us, std::nullopt, std::nullopt, phase});
            continue;
        }
        if (e.type == "TASK_BEGIN") {
            auto it = std::find_if(report.cycles.rbegin(), report.cycles.rend(), [&](const Cycle& cycle) {
                return cycle.task == e.task && !cycle.start_us && cycle.release_us <= e.timestamp_us;
            });
            if (it == report.cycles.rend()) state_error(context + "task begin without release marker");
            else it->start_us = e.timestamp_us;
            continue;
        }
        if (e.type == "TASK_FINISH") {
            auto it = std::find_if(report.cycles.rbegin(), report.cycles.rend(), [&](const Cycle& cycle) {
                return cycle.task == e.task && cycle.start_us && !cycle.end_us;
            });
            if (it == report.cycles.rend()) state_error(context + "task finish without begin marker");
            else it->end_us = e.timestamp_us;
        }
    }
    for (const auto& [name,c] : cores) {
        if (c.isr_depth || !c.active.empty() || !c.paused.empty()) {
            state_error("unclosed scheduler / ISR interval on core " + name);
        }
    }
    for (const auto& [key,open] : pending) {
        (void)open;
        warn("unpaired resource wait: " + key.first + " / " + key.second);
    }
    for (const auto& [name,task] : tasks) { (void)name; report.threads.push_back(task); }
    for (const auto& wait : report.waits) {
        const auto duration = wait.end_us - wait.start_us;
        if (duration == 0) continue;
        report.findings.push_back({"RESOURCE_WAIT", interrupted ? "incomplete" : "observed",
                                   "Observed completed " + wait.resource_type + " wait; no deadline inferred",
                                   wait.task, wait.resource, wait.start_us, wait.end_us, duration});
        if (interrupted || !report.complete || !wait.owner_proven || wait.resource_type != "mutex") continue;
        const auto high = priority_at(priorities, wait.task, wait.start_us);
        const auto low = priority_at(priorities, wait.owner, wait.start_us);
        if (!high || !low || *high <= *low) continue;
        // A dynamic priority change can change the interpretation of the wait.
        const auto priority_changed = [&](const std::string& task) {
            const auto& history = priorities.at(task);
            return std::any_of(history.begin(), history.end(), [&](const auto& record) {
                return record.timestamp > wait.start_us && record.timestamp <= wait.end_us;
            });
        };
        if (priority_changed(wait.task) || priority_changed(wait.owner)) continue;
        std::uint64_t medium_us = 0;
        for (const auto& span : report.intervals) {
            if (span.state != "running" || span.task == wait.task || span.task == wait.owner) continue;
            const auto medium = priority_at(priorities, span.task, span.start_us);
            if (!medium || !(*low < *medium && *medium < *high)) continue;
            const auto begin = std::max(span.start_us, wait.start_us);
            const auto end = std::min(span.end_us, wait.owner_release_us.value_or(wait.end_us));
            if (end > begin) medium_us += end - begin;
        }
        if (medium_us != 0) {
            report.findings.push_back({"PRIORITY_INVERSION_PATTERN", "observed-pattern",
                "High-priority waiter blocked on lower-priority mutex owner while medium-priority task executed; causation and deadline miss not proven",
                wait.task, wait.resource, wait.start_us, wait.end_us, medium_us});
        }
    }
    for (const auto& cycle : report.cycles) {
        if (cycle.start_us && *cycle.start_us >= cycle.release_us && *cycle.start_us != cycle.release_us)
            report.findings.push_back({"DISPATCH_DELAY", interrupted ? "incomplete" : "observed",
                "Time between explicit release and task-begin marker (application-level, not necessarily scheduler dispatch)",
                cycle.task, "-", cycle.release_us, *cycle.start_us, *cycle.start_us - cycle.release_us});
    }
    return report;
}

std::string report_json(const Report& r) {
    std::ostringstream s;
    s << "{\n  \"schema\": \"rtos-observatory-report/v1\",\n"
      << "  \"session\": " << json_quote(r.session) << ",\n"
      << "  \"source\": " << json_quote(r.source) << ",\n"
      << "  \"complete\": " << (r.complete ? "true" : "false") << ",\n"
      << "  \"start_us\": " << r.start_us << ", \"end_us\": " << r.end_us << ",\n";
    s << "  \"threads\": [";
    for (std::size_t i=0;i<r.threads.size();++i) {
        if (i) s << ',';
        const auto& t=r.threads[i];
        s << "{\"id\":" << json_quote(t.id) << ",\"priority\":";
        if (t.priority) s << *t.priority; else s << "null";
        s << ",\"period_us\":";
        if (t.period_us) s << *t.period_us; else s << "null";
        s << ",\"idle\":" << (t.idle ? "true" : "false") << '}';
    }
    s << "],\n  \"intervals\": [";
    for (std::size_t i=0;i<r.intervals.size();++i) {
        if (i) s << ',';
        const auto& v=r.intervals[i];
        s << "{\"task\":" << json_quote(v.task) << ",\"core\":" << json_quote(v.core)
          << ",\"state\":" << json_quote(v.state) << ",\"start_us\":" << v.start_us
          << ",\"end_us\":" << v.end_us << '}';
    }
    s << "],\n  \"waits\": [";
    for (std::size_t i=0;i<r.waits.size();++i) {
        if (i) s << ',';
        const auto& w=r.waits[i];
        s << "{\"task\":" << json_quote(w.task) << ",\"resource\":" << json_quote(w.resource)
          << ",\"resource_type\":" << json_quote(w.resource_type) << ",\"owner\":" << json_quote(w.owner)
          << ",\"owner_proven\":" << (w.owner_proven ? "true" : "false") << ",\"owner_release_us\":" << (w.owner_release_us ? std::to_string(*w.owner_release_us) : "null") << ",\"start_us\":"
          << w.start_us << ",\"end_us\":" << w.end_us << '}';
    }
    s << "],\n  \"cycles\": [";
    for (std::size_t i=0;i<r.cycles.size();++i) {
        if (i) s << ',';
        const auto& c=r.cycles[i];
        s << "{\"task\":" << json_quote(c.task) << ",\"release_us\":" << c.release_us
          << ",\"start_us\":";
        if (c.start_us) s << *c.start_us; else s << "null";
        s << ",\"end_us\":";
        if (c.end_us) s << *c.end_us; else s << "null";
        s << ",\"phase_error_us\":";
        if (c.phase_error_us) s << *c.phase_error_us; else s << "null";
        s << '}';
    }
    s << "],\n  \"findings\": [";
    for (std::size_t i=0;i<r.findings.size();++i) {
        if (i) s << ',';
        const auto& f=r.findings[i];
        s << "{\"code\":" << json_quote(f.code) << ",\"confidence\":" << json_quote(f.confidence)
          << ",\"description\":" << json_quote(f.description) << ",\"task\":" << json_quote(f.task)
          << ",\"resource\":" << json_quote(f.resource) << ",\"start_us\":" << f.start_us
          << ",\"end_us\":" << f.end_us << ",\"observed_us\":" << f.observed_us << '}';
    }
    s << "],\n  \"warnings\": [";
    for (std::size_t i=0;i<r.warnings.size();++i) {
        if (i) s << ',';
        s << json_quote(r.warnings[i]);
    }
    s << "]\n}\n";
    return s.str();
}
} // namespace observatory
