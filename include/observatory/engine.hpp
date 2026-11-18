// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace observatory {
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Event {
    std::uint64_t timestamp_us{};
    std::string type;
    std::string task;
    std::string object;
    std::int64_t value{};
    std::size_t line{};
};
struct Capture {
    std::string session;
    std::string source;
    std::vector<Event> events;
};
struct Thread {
    std::string id;
    std::optional<int> priority;
    std::optional<std::uint64_t> period_us;
    bool idle{false};
};
struct Interval {
    std::string task;
    std::string core;
    std::string state;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
};
struct Wait {
    std::string task;
    std::string resource;
    std::string resource_type;
    std::string owner;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
    bool owner_proven{false};
    std::optional<std::uint64_t> owner_release_us;
};
struct Cycle {
    std::string task;
    std::uint64_t release_us{};
    std::optional<std::uint64_t> start_us;
    std::optional<std::uint64_t> end_us;
    std::optional<std::int64_t> phase_error_us;
};
struct Finding {
    std::string code;
    std::string confidence;
    std::string description;
    std::string task;
    std::string resource;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
    std::uint64_t observed_us{};
};
struct Report {
    std::string session;
    std::string source;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
    bool complete{true};
    std::vector<Thread> threads;
    std::vector<Interval> intervals;
    std::vector<Wait> waits;
    std::vector<Cycle> cycles;
    std::vector<Finding> findings;
    std::vector<std::string> warnings;
};
namespace detail {
Event parse_event_row(const std::string& row, std::size_t line);
void append_event(Capture&, Event, std::size_t max_events);
}
Capture parse_trace(std::istream& input, std::size_t max_events = 2000000);
Report analyze(const Capture& capture);
std::string report_json(const Report& report);
} // namespace observatory
