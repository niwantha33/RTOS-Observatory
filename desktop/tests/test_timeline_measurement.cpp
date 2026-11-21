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
    assert(m.selected == 1);
    timeline_set_cursor(m, 1, {1001600, {}, false});
    assert(timeline_cursor_delta(m) && *timeline_cursor_delta(m) == 600);
    timeline_set_cursor(m, 1, {1000200, {}, false});
    assert(*timeline_cursor_delta(m) == 800); // B before A, exact absolute delta
    timeline_clear_cursors(m);
    assert(!timeline_cursor_delta(m) && m.selected == 0);

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
