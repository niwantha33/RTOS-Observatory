// SPDX-License-Identifier: Apache-2.0
#pragma once

// Presentation-only viewport and clock formatting. Never changes source
// timestamps or promotes a manually supplied frequency to a qualified clock.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace observatory::desktop {
struct TimelineViewport {
    std::uint64_t origin{};
    std::uint64_t capture_end{};
    double start{}; // offsets from origin preserve precision for large timestamps
    double width{1.};
    bool initialized{};
    bool fit_to_capture{true};
};

inline double timeline_total(const TimelineViewport &v) noexcept {
    return v.capture_end >= v.origin
        ? std::max(1., static_cast<double>(v.capture_end - v.origin)) : 1.;
}
inline void timeline_fit(TimelineViewport &v, std::uint64_t first,
                         std::uint64_t last) noexcept {
    v.origin = first;
    v.capture_end = std::max(first, last);
    v.start = 0.;
    v.width = timeline_total(v);
    v.initialized = true;
    v.fit_to_capture = true;
}
inline void timeline_sync(TimelineViewport &v, std::uint64_t first,
                          std::uint64_t last) noexcept {
    if (!v.initialized || first != v.origin || last < first || last < v.capture_end) {
        timeline_fit(v, first, last);
        return;
    }
    v.capture_end = last;
    const double total = timeline_total(v);
    if (v.fit_to_capture) {
        v.start = 0.;
        v.width = total;
    } else {
        v.width = std::clamp(v.width, 1., total);
        v.start = std::clamp(v.start, 0., std::max(0., total - v.width));
    }
}
inline void timeline_zoom(TimelineViewport &v, double factor,
                          double anchor_fraction = .5) noexcept {
    if (!v.initialized || !std::isfinite(factor) || factor <= 0.) return;
    const double total = timeline_total(v);
    const double anchor = std::clamp(anchor_fraction, 0., 1.);
    const double width = std::clamp(v.width * factor, 1., total);
    const double point = v.start + v.width * anchor;
    v.width = width;
    v.start = std::clamp(point - width * anchor, 0., std::max(0., total - width));
    v.fit_to_capture = v.start == 0. && v.width == total;
}
inline void timeline_pan(TimelineViewport &v, double fraction_of_window) noexcept {
    if (!v.initialized || !std::isfinite(fraction_of_window)) return;
    const double limit = std::max(0., timeline_total(v) - v.width);
    v.start = std::clamp(v.start + fraction_of_window * v.width, 0., limit);
    v.fit_to_capture = v.start == 0. && v.width == timeline_total(v);
}
inline void timeline_focus(TimelineViewport &v, std::uint64_t first,
                           std::uint64_t last) noexcept {
    if (!v.initialized || last < first) return;
    const double total = timeline_total(v);
    const double from = first > v.origin ? static_cast<double>(first - v.origin) : 0.;
    const double to = last > v.origin ? static_cast<double>(last - v.origin) : 0.;
    if (from >= total || to < 0.) return;
    const double duration = std::max(1., to - from);
    const double margin = std::max(1., duration * .15);
    v.width = std::clamp(duration + 2. * margin, 1., total);
    v.start = std::clamp(from - margin, 0., std::max(0., total - v.width));
    v.fit_to_capture = v.start == 0. && v.width == total;
}

enum class TimelineTimeUnit { Source, Microseconds, Milliseconds, Seconds };
inline const char *timeline_unit_name(TimelineTimeUnit unit,
                                      bool source_is_microseconds) noexcept {
    switch (unit) {
    case TimelineTimeUnit::Source: return source_is_microseconds ? "us" : "ticks";
    case TimelineTimeUnit::Microseconds: return "us";
    case TimelineTimeUnit::Milliseconds: return "ms";
    case TimelineTimeUnit::Seconds: return "s";
    }
    return "ticks";
}
inline bool timeline_has_time_units(bool source_is_microseconds,
                                    std::uint64_t manual_hz) noexcept {
    return source_is_microseconds || manual_hz != 0;
}
inline TimelineTimeUnit timeline_auto_unit(double visible_ticks,
                                          bool source_is_microseconds,
                                          std::uint64_t manual_hz) noexcept {
    if (!timeline_has_time_units(source_is_microseconds, manual_hz))
        return TimelineTimeUnit::Source;
    const double duration_us = source_is_microseconds ? visible_ticks
        : visible_ticks / static_cast<double>(manual_hz) * 1'000'000.;
    if (duration_us >= 1'000'000.) return TimelineTimeUnit::Seconds;
    if (duration_us >= 1'000.) return TimelineTimeUnit::Milliseconds;
    return TimelineTimeUnit::Microseconds;
}
inline double timeline_time_value(double ticks, TimelineTimeUnit unit,
                                  bool source_is_microseconds,
                                  std::uint64_t manual_hz) noexcept {
    if (unit == TimelineTimeUnit::Source) return ticks;
    if (!timeline_has_time_units(source_is_microseconds, manual_hz)) return ticks;
    const double us = source_is_microseconds ? ticks
        : ticks / static_cast<double>(manual_hz) * 1'000'000.;
    switch (unit) {
    case TimelineTimeUnit::Microseconds: return us;
    case TimelineTimeUnit::Milliseconds: return us / 1'000.;
    case TimelineTimeUnit::Seconds: return us / 1'000'000.;
    case TimelineTimeUnit::Source: return ticks;
    }
    return ticks;
}
inline std::string timeline_format(double ticks, TimelineTimeUnit unit,
                                   bool source_is_microseconds,
                                   std::uint64_t manual_hz) {
    const double value = timeline_time_value(ticks, unit,
                                             source_is_microseconds, manual_hz);
    std::ostringstream out;
    out << std::fixed;
    if (unit == TimelineTimeUnit::Source && !source_is_microseconds)
        out << std::setprecision(0);
    else if (std::abs(value) >= 100.)
        out << std::setprecision(1);
    else if (std::abs(value) >= 1.)
        out << std::setprecision(2);
    else
        out << std::setprecision(3);
    out << value << ' ' << timeline_unit_name(unit, source_is_microseconds);
    return out.str();
}
} // namespace observatory::desktop
