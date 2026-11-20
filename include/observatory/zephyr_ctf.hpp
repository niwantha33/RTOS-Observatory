// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace observatory::ctf {
struct Event {
    std::string name;
    std::uint64_t tick{};
    std::size_t byte_offset{};
    std::int64_t actor{-1}; // Currently observed thread, not a CTF object identity.
    std::map<std::string, std::string> fields;
};
struct Finding {
    std::string kind;
    std::string title;
    std::string assessment;
    std::vector<std::size_t> evidence;
    std::uint64_t start_tick{}, end_tick{};
    bool complete{};
};
struct Session {
    std::vector<Event> events;
    std::vector<Finding> findings;
    std::map<std::uint64_t, std::string> thread_names;
    std::size_t pending_bytes{};
    std::size_t bytes_consumed{};
    std::size_t timestamp_wraps{};
    bool complete_file{};
    // CTF stream alone does not prove frequency or loss-free capture.
    bool clock_calibrated{false};
    bool loss_qualified{false};
};
Session open(const std::filesystem::path &directory, bool allow_incomplete_tail = false);
std::string describe_field(const Event &event, const std::string &name);
} // namespace observatory::ctf
