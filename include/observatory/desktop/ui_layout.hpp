// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cmath>

namespace observatory::desktop {
enum class WorkbenchLayout { Compact, Split, Wide };
struct LayoutPolicy {
    WorkbenchLayout mode{WorkbenchLayout::Compact};
    float sidebar_width{};
    float content_width{};
    bool show_sidebar{};
    bool show_two_columns{};
    int metric_columns{1};
};

// Use font-size units rather than absolute pixel breakpoints. The same window
// becomes compact when its text is enlarged or when it moves to a high-DPI
// monitor; no tiny columns and no clipped floating controls.
inline LayoutPolicy plan_layout(float available_width, float font_pixels,
                                bool navigation_enabled = true) noexcept {
    const float width = std::max(0.f, available_width);
    const float em = std::clamp(font_pixels, 12.f, 80.f);
    const float rem = width / em;
    LayoutPolicy out;
    out.mode = rem >= 87.f ? WorkbenchLayout::Wide :
               rem >= 52.f ? WorkbenchLayout::Split : WorkbenchLayout::Compact;
    out.show_sidebar = navigation_enabled && out.mode != WorkbenchLayout::Compact;
    out.sidebar_width = out.show_sidebar
        ? std::clamp(em * 15.0f, em * 12.f, width * .31f) : 0.f;
    out.content_width = std::max(0.f, width - out.sidebar_width - em);
    out.show_two_columns = out.content_width >= 43.f * em;
    out.metric_columns = out.content_width >= 65.f * em ? 4 :
                         out.content_width >= 32.f * em ? 2 : 1;
    return out;
}

inline float clamp_text_zoom(float desired) noexcept {
    return std::isfinite(desired) ? std::clamp(desired, 1.0f, 2.2f) : 1.2f;
}
} // namespace observatory::desktop
