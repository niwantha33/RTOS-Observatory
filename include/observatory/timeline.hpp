// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace observatory {
// All boundaries are half-open [start_us, end_us); "unknown" is never idle.
// A CPU percentage is available ONLY for fully covered, loss-free windows.
struct TimelineSlice {
    std::string core;
    std::string task;
    std::string state; // running, idle, isr, unknown
    std::uint64_t start_us{};
    std::uint64_t end_us{};
};
struct WaitSlice {
    std::string task;
    std::string resource;
    std::string kind;
    std::uint64_t start_us{};
    std::uint64_t end_us{};
};
struct CoreUsage {
    std::string core;
    std::uint64_t running_us{};
    std::uint64_t idle_us{};
    std::uint64_t isr_us{};
    std::uint64_t unknown_us{};
    std::optional<double> busy_percent;
};
struct TaskUsage {
    std::string task;
    std::uint64_t running_us{};
    std::uint64_t completed_wait_us{};
    std::size_t completed_waits{};
};
struct TimelineView {
    std::uint64_t start_us{};
    std::uint64_t end_us{};
    bool evidence_complete{false};
    bool rendering_limited{false};
    std::vector<TimelineSlice> slices;
    std::vector<WaitSlice> waits;
    std::vector<CoreUsage> cores;
    std::vector<TaskUsage> tasks;
    std::vector<std::string> warnings;
};
// Takes an existing analyzed report; no scheduler state is inferred from missing
// events. evidence_complete additionally covers decoder sequence/loss metadata.
// The optional cap affects ONLY display slices, never duration/accounting totals.
TimelineView build_timeline(const Report& report, std::uint64_t start_us,
                            std::uint64_t end_us, bool evidence_complete,
                            std::size_t render_limit = 20000);
} // namespace observatory
