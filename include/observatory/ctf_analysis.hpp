// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/zephyr_ctf.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace observatory::ctf {
// A source-indexed view, NOT the normalized microsecond timeline contract.
// Recorded callbacks delimit spans; no CPU-load or frequency claim.
struct AnalysisSpan {
    std::string kind; // "Thread" or "ISR"
    std::string subject;
    std::uint64_t start_tick{};
    std::uint64_t end_tick{};
    std::size_t begin_event{};
    std::size_t end_event{};
};
struct AnalysisIndex {
    std::vector<AnalysisSpan> thread_spans;
    std::vector<AnalysisSpan> isr_spans;
    std::vector<std::size_t> scheduler_events;
    std::size_t unpaired_thread_switches{};
    std::size_t unpaired_isr_callbacks{};
    std::size_t resource_cases{};
    bool truncated{};
    bool time_calibrated{false};
    bool full_coverage_verified{false};
};
AnalysisIndex build_analysis_index(const Session& source,
                                   std::size_t maximum_events = 1000000,
                                   std::size_t maximum_spans = 50000);
} // namespace observatory::ctf
