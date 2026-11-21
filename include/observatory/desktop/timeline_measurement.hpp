// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/timeline_navigation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace observatory::desktop {

// 64-bit source ticks are authoritative. Clock frequency is a UI-only estimate,
// never inferred from CPU MHz, and never becomes independently calibrated.
enum class TimelineClockSource {
    Unknown, CpuCycleCounter, PeripheralTimer, KernelTick, External, Other
};
inline const char* timeline_clock_source_label(TimelineClockSource source) noexcept {
    switch (source) {
    case TimelineClockSource::Unknown: return "Unknown";
    case TimelineClockSource::CpuCycleCounter: return "CPU cycle counter";
    case TimelineClockSource::PeripheralTimer: return "Hardware timer";
    case TimelineClockSource::KernelTick: return "RTOS timer tick";
    case TimelineClockSource::External: return "External clock";
    case TimelineClockSource::Other: return "Other known counter";
    }
    return "Unknown";
}
inline bool timeline_clock_hz_valid(int source, bool enabled,
                                   std::uint64_t hz) noexcept {
    return enabled && source >= 1 && source <= 5 && hz >= 1 &&
           hz <= 10'000'000'000ULL;
}
struct TimelineCursorPoint {
    std::uint64_t tick{};
    std::optional<std::size_t> event_index;
    bool snapped{};
};
// A1/A2 and B1/B2 form independent measurement pairs.
inline constexpr std::array<const char *, 4> timeline_cursor_labels{
    "A1", "A2", "B1", "B2"
};
struct TimelineMeasurement {
    bool enabled{};
    bool snap{true};
    int selected{}; // 0=A1, 1=A2, 2=B1, 3=B2
    std::array<std::optional<TimelineCursorPoint>, 4> points{};
};
inline std::uint64_t timeline_tick_at_fraction(const TimelineViewport &view,
                                                double fraction) noexcept {
    if (!view.initialized || view.capture_end < view.origin)
        return view.origin;
    const double frac = std::isfinite(fraction) ? std::clamp(fraction, 0., 1.) : 0.;
    const double total = timeline_total(view);
    const double offset = std::clamp(view.start + view.width * frac, 0., total);
    const auto range = view.capture_end - view.origin;
    if (offset >= static_cast<double>(range))
        return view.capture_end;
    // Offset is bounded to the capture's representable delta before integer cast.
    const auto delta = static_cast<std::uint64_t>(std::floor(offset + .5));
    if (delta >= range)
        return view.capture_end;
    return view.origin + delta;
}
inline std::uint64_t timeline_tick_distance(std::uint64_t a,
                                            std::uint64_t b) noexcept {
    return a >= b ? a - b : b - a;
}
inline std::optional<std::uint64_t> timeline_cursor_delta(
    const TimelineMeasurement &measurement, int pair = 0) noexcept {
    if (pair < 0 || pair > 1)
        return std::nullopt;
    const auto &first = measurement.points[static_cast<std::size_t>(pair * 2)];
    const auto &last = measurement.points[static_cast<std::size_t>(pair * 2 + 1)];
    if (!first || !last)
        return std::nullopt;
    return timeline_tick_distance(first->tick, last->tick);
}

// Exact, non-overlapping active spans for an observed digital activity waveform.
// Missing time between spans is NOT evidence of Ready/Sleeping/Idle state.
struct TimelineWaveInterval {
    std::uint64_t start{};
    std::uint64_t end{};
};
inline std::vector<TimelineWaveInterval> timeline_waveform_intervals(
    const std::vector<TimelineWaveInterval> &observed,
    std::uint64_t visible_start, std::uint64_t visible_end) {
    std::vector<TimelineWaveInterval> result;
    if (visible_end <= visible_start)
        return result;
    result.reserve(observed.size());
    for (const auto &span : observed) {
        if (span.end <= span.start || span.end <= visible_start ||
            span.start >= visible_end)
            continue;
        result.push_back({std::max(span.start, visible_start),
                          std::min(span.end, visible_end)});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) {
                  return a.start < b.start ||
                         (a.start == b.start && a.end < b.end);
              });
    std::size_t merged = 0;
    for (const auto &span : result) {
        if (merged && span.start <= result[merged - 1].end)
            result[merged - 1].end = std::max(result[merged - 1].end, span.end);
        else
            result[merged++] = span;
    }
    result.resize(merged);
    return result;
}

// Source/event indices are sorted once at import; hover/drag is logarithmic.
// Only a snap within the requested pixel tolerance is accepted.
inline TimelineCursorPoint timeline_snap_tick(
    const TimelineViewport &view, std::uint64_t proposed, float pixels,
    const std::vector<std::uint64_t> &edges,
    const std::vector<std::pair<std::uint64_t, std::size_t>> &events,
    float tolerance_pixels = 8.f) {
    TimelineCursorPoint result{proposed, std::nullopt, false};
    if (!view.initialized || !(pixels > 0.f) || !(tolerance_pixels > 0.f))
        return result;
    const double threshold = std::max(1.,
        view.width * static_cast<double>(tolerance_pixels) / pixels);
    std::uint64_t best_distance = std::numeric_limits<std::uint64_t>::max();
    const auto consider = [&](std::uint64_t tick, std::optional<std::size_t> index) {
        if (tick < view.origin || tick > view.capture_end)
            return;
        const auto delta = timeline_tick_distance(proposed, tick);
        if (static_cast<double>(delta) > threshold || delta >= best_distance)
            return;
        best_distance = delta;
        result = {tick, index, true};
    };
    const auto it = std::lower_bound(edges.begin(), edges.end(), proposed);
    if (it != edges.end()) consider(*it, std::nullopt);
    if (it != edges.begin()) consider(*std::prev(it), std::nullopt);

    const auto event_it = std::lower_bound(
        events.begin(), events.end(), proposed,
        [](const auto &entry, std::uint64_t tick) { return entry.first < tick; });
    // Prefer an event if its distance is equal to a span-edge snap.
    const auto consider_event = [&](const auto &entry) {
        const auto delta = timeline_tick_distance(proposed, entry.first);
        if (entry.first < view.origin || entry.first > view.capture_end ||
            static_cast<double>(delta) > threshold || delta > best_distance)
            return;
        best_distance = delta;
        result = {entry.first, entry.second, true};
    };
    if (event_it != events.end()) consider_event(*event_it);
    if (event_it != events.begin()) consider_event(*std::prev(event_it));
    return result;
}
inline void timeline_set_cursor(TimelineMeasurement &measurement, int which,
                                TimelineCursorPoint value) noexcept {
    if (which < 0 || which >= static_cast<int>(measurement.points.size()))
        return;
    measurement.points[static_cast<std::size_t>(which)] = value;
    // Cycle to the next unset handle; retain the selected handle when dragging.
    if (measurement.selected == which) {
        for (int step = 1; step <= 4; ++step) {
            const int next = (which + step) % 4;
            if (!measurement.points[static_cast<std::size_t>(next)]) {
                measurement.selected = next;
                break;
            }
        }
    }
}
inline void timeline_clear_cursors(TimelineMeasurement &measurement) noexcept {
    measurement.points.fill(std::nullopt);
    measurement.selected = 0;
}
} // namespace observatory::desktop
