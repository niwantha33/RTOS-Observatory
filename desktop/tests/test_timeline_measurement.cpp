// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/timeline_measurement.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace observatory::desktop;
int main() {
    TimelineViewport view;
    timeline_fit(view, 1000000, 1002000);
    assert(timeline_tick_at_fraction(view, 0.) == 1000000);
    assert(timeline_tick_at_fraction(view, .5) == 1001000);
    assert(timeline_tick_at_fraction(view, 1.) == 1002000);
    assert(timeline_tick_at_fraction(view, -7.) == 1000000);
    assert(timeline_tick_at_fraction(view, 7.) == 1002000);
    assert(timeline_tick_at_fraction(view, std::numeric_limits<double>::quiet_NaN()) == 1000000);

    TimelineMeasurement m;
    m.enabled = true;
    std::vector<std::uint64_t> edges{1000200, 1000600, 1001000, 1001600};
    std::vector<std::pair<std::uint64_t, std::size_t>> events{{1000700, 3}, {1001000, 8}};
    // Screen = 1000px; 8px tolerance = 16 ticks.
    auto snapped = timeline_snap_tick(view, 1000995, 1000.f, edges, events);
    assert(snapped.snapped && snapped.tick == 1001000);
    assert(snapped.event_index && *snapped.event_index == 8);
    auto free = timeline_snap_tick(view, 1001250, 1000.f, edges, events);
    assert(!free.snapped && free.tick == 1001250);
    timeline_set_cursor(m, 0, snapped);
    assert(m.selected == 1); // A2 automatically follows A1
    timeline_set_cursor(m, 1, {1001600, {}, false});
    assert(timeline_cursor_delta(m, 0) && *timeline_cursor_delta(m, 0) == 600);
    assert(m.selected == 2);
    timeline_set_cursor(m, 2, {1000400, {}, false});
    assert(m.selected == 3);
    timeline_set_cursor(m, 3, {1000800, {}, false});
    assert(timeline_cursor_delta(m, 1) && *timeline_cursor_delta(m, 1) == 400);
    assert(!timeline_cursor_delta(m, -1));
    assert(!timeline_cursor_delta(m, 2));
    timeline_set_cursor(m, 1, {1000200, {}, false});
    assert(*timeline_cursor_delta(m) == 800); // A2 before A1: absolute delta
    timeline_set_cursor(m, 5, {0, {}, false}); // invalid handle is safe
    assert(*timeline_cursor_delta(m, 1) == 400);
    timeline_clear_cursors(m);
    assert(!timeline_cursor_delta(m) && !timeline_cursor_delta(m, 1) &&
           m.selected == 0);
    for (const auto &cursor : m.points) assert(!cursor);

    // Intervals merge on overlap/touch and clip to the viewport.
    const std::vector<TimelineWaveInterval> observed{
        {50, 80}, {10, 30}, {20, 40}, {40, 45}, {80, 90},
        {140, 140}, {135, 120}, {100, 110}
    };
    auto wave = timeline_waveform_intervals(observed, 25, 105);
    assert(wave.size() == 3);
    assert(wave[0].start == 25 && wave[0].end == 45);
    assert(wave[1].start == 50 && wave[1].end == 90);
    assert(wave[2].start == 100 && wave[2].end == 105);
    assert(timeline_waveform_intervals(observed, 10, 10).empty());
    assert(timeline_waveform_intervals(observed, 200, 300).empty());

    // 64-bit absolute raw timestamp remains precise with small window.
    const auto top = std::numeric_limits<std::uint64_t>::max();
    timeline_fit(view, top - 5000, top);
    timeline_zoom(view, .2);
    assert(timeline_tick_at_fraction(view, 0.) == top - 3000);
    assert(timeline_tick_at_fraction(view, 1.) == top - 2000);
    assert(timeline_tick_distance(0, top) == top);
    assert(timeline_clock_hz_valid(1, true, 120000000));
    assert(!timeline_clock_hz_valid(0, true, 120000000));
    assert(!timeline_clock_hz_valid(1, true, 0));
    assert(!timeline_clock_hz_valid(1, false, 120000000));
    assert(!timeline_clock_hz_valid(1, true, 11000000000ULL));
    assert(std::abs(timeline_time_value(120000., TimelineTimeUnit::Milliseconds,
                                        false, 120000000) - 1.) < .0000001);
    return 0;
}
