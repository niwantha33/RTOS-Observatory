// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/timeline_navigation.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

using namespace observatory::desktop;
int main() {
    TimelineViewport v;
    timeline_sync(v, 1'000'000, 9'000'000);
    assert(v.initialized && v.start == 0. && v.width == 8'000'000.);
    timeline_zoom(v, .5, .5);
    assert(v.start == 2'000'000. && v.width == 4'000'000.);
    timeline_pan(v, .25);
    assert(v.start == 3'000'000.);
    timeline_pan(v, 99.);
    assert(v.start == 4'000'000.);
    timeline_zoom(v, 500., .5);
    assert(v.start == 0. && v.fit_to_capture);
    timeline_sync(v, 1'000'000, 11'000'000);
    assert(v.width == 10'000'000.);
    timeline_zoom(v, .2);
    timeline_sync(v, 1'000'000, 14'000'000);
    assert(v.width == 2'000'000.); // keep zoom during live capture append
    timeline_focus(v, 5'000'000, 5'100'000);
    assert(v.width < 200'000. && v.start > 0.);
    timeline_sync(v, 2'000'000, 14'000'000);
    assert(v.fit_to_capture); // new capture resets
    const auto large = std::numeric_limits<std::uint64_t>::max();
    timeline_fit(v, large - 5000, large);
    timeline_zoom(v, .2);
    assert(v.width == 1000. && v.start == 2000.);
    timeline_zoom(v, std::numeric_limits<double>::quiet_NaN());
    assert(v.width == 1000.);
    assert(timeline_auto_unit(5'000'000., false, 0) == TimelineTimeUnit::Source);
    assert(timeline_auto_unit(2'000., true, 0) == TimelineTimeUnit::Milliseconds);
    assert(timeline_auto_unit(200'000'000., false, 100'000'000) == TimelineTimeUnit::Seconds);
    assert(timeline_auto_unit(200., false, 100'000'000) == TimelineTimeUnit::Microseconds);
    assert(std::abs(timeline_time_value(100'000., TimelineTimeUnit::Milliseconds,
                                        false, 100'000'000) - 1.) < 0.00001);
    assert(std::abs(timeline_time_value(20'000., TimelineTimeUnit::Milliseconds,
                                        true, 0) - 20.) < 0.00001);
    assert(timeline_format(120., TimelineTimeUnit::Source, false, 0) == "120 ticks");
    assert(timeline_format(100'000., TimelineTimeUnit::Milliseconds,
                           false, 100'000'000).find("ms") != std::string::npos);
    return 0;
}
