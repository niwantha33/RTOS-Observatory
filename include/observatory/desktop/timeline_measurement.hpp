// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/timeline_navigation.hpp"
#include <algorithm>
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
struct TimelineMeasurement {
    bool enabled{};
    bool snap{true};
    int selected{}; // 0 = A; 1 = B
    std::optional<TimelineCursorPoint> a, b;
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
    const TimelineMeasurement &measurement) noexcept {
    if (!measurement.a || !measurement.b)
        return std::nullopt;
    return timeline_tick_distance(measurement.a->tick, measurement.b->tick);
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
    if (which == 0) {
        measurement.a = value;
        if (!measurement.b) measurement.selected = 1;
    } else {
        measurement.b = value;
    }
}
inline void timeline_clear_cursors(TimelineMeasurement &measurement) noexcept {
    measurement.a.reset();
    measurement.b.reset();
    measurement.selected = 0;
}
} // namespace observatory::desktop
