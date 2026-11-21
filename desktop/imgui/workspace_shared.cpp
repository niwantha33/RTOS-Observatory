// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace observatory::desktop {
namespace {
const char *clock_unit(ClockUnit unit) {
    switch (unit) {
    case ClockUnit::Microseconds: return "us (normalized)";
    case ClockUnit::RawCtfTicks: return "raw CTF ticks";
    case ClockUnit::PublisherTicks: return "publisher ticks / epoch";
    }
    return "unknown";
}
ImU32 category_colour(const std::string &kind) {
    if (kind == "Mutex" || kind == "mutex") return IM_COL32(246, 176, 98, 255);
    if (kind == "Semaphore" || kind == "semaphore") return IM_COL32(85, 200, 166, 255);
    if (kind == "Queue" || kind == "Message queue") return IM_COL32(171, 150, 235, 255);
    if (kind == "ISR" || kind == "isr") return IM_COL32(246, 191, 100, 255);
    if (kind == "idle") return IM_COL32(104, 148, 166, 255);
    if (kind == "unknown") return IM_COL32(111, 121, 140, 255);
    return IM_COL32(83, 169, 228, 255);
}
std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
void metric(const char *label, const std::string &value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SetWindowFontScale(1.f);
}
} // namespace

void Workspace::draw_shared_dashboard() {
    const auto &data = unified_;
    const auto policy = plan_layout(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize(), false);
    const int columns = policy.metric_columns;
    if (ImGui::BeginTable("unified-metrics", columns,
                          ImGuiTableFlags_SizingStretchSame)) {
        const std::array<std::pair<const char *, std::size_t>, 4> all{{
            {"EVENTS", data.total_events}, {"THREADS", data.observed_threads},
            {"RESOURCE OBJECTS", data.objects.size()}, {"RESOURCE CASES", data.cases.size()}
        }};
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i % static_cast<std::size_t>(columns) == 0)
                ImGui::TableNextRow();
            ImGui::TableNextColumn();
            metric(all[i].first, std::to_string(all[i].second));
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Evidence capabilities");
    if (ImGui::BeginTable("unified-capabilities", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        const std::array<std::pair<const char *, ToolCapability>, 4> choices{{
            {"Event browser", ToolCapability::Events},
            {"Graphical intervals", ToolCapability::Timeline},
            {"Resource observations", ToolCapability::Resources},
            {"Qualified CPU usage", ToolCapability::QualifiedCpu}
        }};
        for (const auto &[name, cap] : choices) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            if (has_capability(data.capabilities, cap))
                ImGui::TextColored(ImVec4(.48f, .85f, .7f, 1.f), "Available");
            else
                ImGui::TextDisabled("Not qualified by this adapter");
        }
        ImGui::EndTable();
    }
    if (has_capability(data.capabilities, ToolCapability::QualifiedCpu)) {
        ImGui::SeparatorText("Verified CPU view");
        for (const auto &[name, busy] : data.qualified_cpu) {
            ImGui::TextUnformatted(name.c_str());
            ImGui::ProgressBar(std::clamp(static_cast<float>(busy / 100.), 0.f, 1.f),
                               ImVec2(-1.f, 0.f));
        }
    }
    ImGui::SeparatorText("Observed object categories");
    std::map<std::string, std::size_t> summary;
    if (!data.objects.empty()) {
        for (const auto &object : data.objects)
            ++summary[object.kind];
    } else {
        for (const auto &entry : data.cases)
            ++summary[entry.kind];
    }
    std::size_t maximum = 1;
    for (const auto &[name, count] : summary) {
        (void)name;
        maximum = std::max(maximum, count);
    }
    if (summary.empty())
        ImGui::TextDisabled("No qualified resource cases in this capture.");
    else for (const auto &[name, count] : summary) {
        ImGui::Text("%s  (%zu)", name.c_str(), count);
        ImGui::ProgressBar(static_cast<float>(count) / static_cast<float>(maximum),
                           ImVec2(-1.f, 0.f), "");
    }
    if (has_capability(data.capabilities, ToolCapability::Timeline)) {
        if (ImGui::Button("View graphical timeline"))
            page_ = Page::Timeline;
        ImGui::SameLine();
    }
    if (ImGui::Button("Explore source events"))
        page_ = Page::Events;
}

void Workspace::draw_shared_timeline() {
    const auto &data = unified_;
    if (!has_capability(data.capabilities, ToolCapability::Timeline)) {
        ImGui::TextWrapped("This adapter has no qualified time intervals. "
                           "Publisher ticks from distinct epochs cannot be combined.");
        if (ImGui::Button("Open source events"))
            page_ = Page::Events;
        return;
    }
    if (data.spans.empty() || data.lane_index.empty()) {
        ImGui::TextDisabled("No recorded complete intervals. Missing transitions are not inferred.");
        return;
    }
    // Keep the original integer capture origin; double is used ONLY for small
    // relative offsets in pixels so 64-bit raw ticks remain source-exact.
    std::uint64_t first = data.spans.front().start;
    std::uint64_t last = data.spans.front().end;
    for (const auto &span : data.spans) {
        if (span.end < span.start) continue;
        first = std::min(first, span.start);
        last = std::max(last, span.end);
    }
    timeline_sync(timeline_viewport_, first, last);
    const bool native_us = data.unit == ClockUnit::Microseconds;
    const bool raw_ctf = data.unit == ClockUnit::RawCtfTicks;
    if (ImGui::Button("Zoom in")) timeline_zoom(timeline_viewport_, .5);
    ImGui::SameLine();
    if (ImGui::Button("Zoom out")) timeline_zoom(timeline_viewport_, 2.);
    ImGui::SameLine();
    if (ImGui::Button("Fit all")) timeline_fit(timeline_viewport_, first, last);
    if (shared_selected_case_ && *shared_selected_case_ < data.cases.size()) {
        ImGui::SameLine();
        if (ImGui::Button("Focus finding")) {
            const auto &item = data.cases[*shared_selected_case_];
            timeline_focus(timeline_viewport_, item.start, item.end);
        }
    }

    // Keep the time-base summary visible, but make the detailed settings collapsible.
    if (raw_ctf) {
        const bool known = timeline_clock_hz_valid(timeline_clock_source_,
            timeline_manual_clock_enabled_, timeline_manual_clock_hz_);
        if (known)
            ImGui::TextColored(ImVec4(.99f, .77f, .48f, 1.f),
                "Time base: manual %s @ %llu Hz (UNVERIFIED estimate)",
                timeline_clock_source_label(static_cast<TimelineClockSource>(
                    timeline_clock_source_)),
                static_cast<unsigned long long>(timeline_manual_clock_hz_));
        else
            ImGui::TextColored(ImVec4(.99f, .77f, .48f, 1.f),
                "Time base: raw CTF ticks; no verified counter frequency");
        if (ImGui::CollapsingHeader("Clock configuration (counter and frequency)")) {
            ImGui::TextDisabled("Zephyr CTF 32-bit counter (unwrapped).");
            constexpr const char *clock_sources[] = {
                "Unknown", "CPU cycle counter", "Hardware timer",
                "RTOS timer tick", "External clock", "Other known counter"
            };
            ImGui::SetNextItemWidth(205.f);
            ImGui::Combo("Timestamp source", &timeline_clock_source_,
                         clock_sources, IM_ARRAYSIZE(clock_sources));
            ImGui::Checkbox("Apply manual clock frequency",
                            &timeline_manual_clock_enabled_);
            if (timeline_manual_clock_enabled_) {
                ImGui::SetNextItemWidth(205.f);
                ImGui::InputScalar("Effective timestamp Hz", ImGuiDataType_U64,
                    &timeline_manual_clock_hz_, nullptr, nullptr, "%llu");
            }
            ImGui::TextDisabled("Enter effective counter Hz after prescaler, NOT CPU MHz."
                                " This does not calibrate the trace.");
        }
    } else if (native_us) {
        ImGui::TextDisabled("Time base: source-declared microseconds (not calibrated).");
    }

    const bool valid_manual_hz =
        timeline_clock_hz_valid(timeline_clock_source_, timeline_manual_clock_enabled_,
                                timeline_manual_clock_hz_);
    const std::uint64_t hz = raw_ctf && valid_manual_hz ? timeline_manual_clock_hz_ : 0;
    const bool convertible = timeline_has_time_units(native_us, hz);

    constexpr std::array<const char*, 5> choices{"Auto", "Source units", "us", "ms", "s"};
    timeline_unit_preference_ = std::clamp(timeline_unit_preference_, 0, 4);
    ImGui::SetNextItemWidth(145.f);
    if (ImGui::BeginCombo("Time units", choices[static_cast<std::size_t>(timeline_unit_preference_)])) {
        for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
            const bool available = i < 2 || convertible;
            if (!available) ImGui::BeginDisabled();
            if (ImGui::Selectable(choices[static_cast<std::size_t>(i)],
                                  timeline_unit_preference_ == i))
                timeline_unit_preference_ = i;
            if (!available) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    TimelineTimeUnit unit = TimelineTimeUnit::Source;
    if (timeline_unit_preference_ == 0)
        unit = timeline_auto_unit(timeline_viewport_.width, native_us, hz);
    else if (timeline_unit_preference_ == 2 && convertible)
        unit = TimelineTimeUnit::Microseconds;
    else if (timeline_unit_preference_ == 3 && convertible)
        unit = TimelineTimeUnit::Milliseconds;
    else if (timeline_unit_preference_ == 4 && convertible)
        unit = TimelineTimeUnit::Seconds;

    const double total = timeline_total(timeline_viewport_);
    const double max_pan = std::max(0., total - timeline_viewport_.width);
    if (max_pan > 0.) {
        float position = static_cast<float>(timeline_viewport_.start / max_pan);
        ImGui::SetNextItemWidth(std::min(360.f, ImGui::GetContentRegionAvail().x * .7f));
        if (ImGui::SliderFloat("Horizontal position", &position, 0.f, 1.f, "%.0f%%")) {
            timeline_viewport_.start = max_pan * static_cast<double>(position);
            timeline_viewport_.fit_to_capture = false;
        }
    }
    const auto beginning = timeline_format(timeline_viewport_.start, unit, native_us, hz);
    const auto ending = timeline_format(timeline_viewport_.start + timeline_viewport_.width,
                                        unit, native_us, hz);
    ImGui::TextDisabled("Visible: +%s to +%s  |  capture origin: %llu %s",
                        beginning.c_str(), ending.c_str(),
                        static_cast<unsigned long long>(first), clock_unit(data.unit));
    ImGui::TextDisabled("Ctrl+mouse wheel: zoom at pointer   |   Shift+wheel / middle-drag: pan");
    if (!convertible && raw_ctf)
        ImGui::TextColored(ImVec4(.99f, .77f, .48f, 1.f),
                           "Raw ticks only: source clock Hz is not verified or supplied.");

    ImGui::SeparatorText("Measurement cursors");
    auto &measure = timeline_measurement_;
    ImGui::Checkbox("Enable four cursors", &measure.enabled);
    if (measure.enabled) {
        ImGui::SameLine();
        ImGui::Checkbox("Snap to event / edge", &measure.snap);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear all"))
            timeline_clear_cursors(measure);

        const std::array<ImVec4, 4> colors{{
            ImVec4(1.f, .76f, .31f, 1.f), ImVec4(1.f, .53f, .30f, 1.f),
            ImVec4(.33f, .87f, 1.f, 1.f), ImVec4(.45f, .63f, 1.f, 1.f)
        }};
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, colors[static_cast<std::size_t>(i)]);
            if (ImGui::RadioButton(timeline_cursor_labels[static_cast<std::size_t>(i)],
                                   measure.selected == i))
                measure.selected = i;
            ImGui::PopStyleColor();
        }
        ImGui::TextDisabled("Click ruler or track to place; drag marker to move."
                            " A1-A2 and B1-B2 are independent pairs.");

        const int columns = ImGui::GetContentRegionAvail().x < 650.f ? 2 : 4;
        if (ImGui::BeginTable("cursor-readouts", columns,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
            for (int i = 0; i < 4; ++i) {
                ImGui::TableNextColumn();
                const auto &point = measure.points[static_cast<std::size_t>(i)];
                ImGui::PushStyleColor(ImGuiCol_Text, colors[static_cast<std::size_t>(i)]);
                if (point) {
                    const auto offset = point->tick >= first ? point->tick - first : 0;
                    const auto value = timeline_format(
                        static_cast<double>(offset), unit, native_us, hz);
                    ImGui::Text("%s  +%s", timeline_cursor_labels[static_cast<std::size_t>(i)],
                                value.c_str());
                    ImGui::TextDisabled("%llu ticks",
                         static_cast<unsigned long long>(point->tick));
                } else {
                    ImGui::Text("%s  --", timeline_cursor_labels[static_cast<std::size_t>(i)]);
                    ImGui::TextDisabled("Not placed");
                }
                ImGui::PopStyleColor();
                if (point && point->event_index &&
                    *point->event_index < data.events.size()) {
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("Inspect event")) {
                        shared_selected_event_ = *point->event_index;
                        page_ = Page::Evidence;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        for (int pair = 0; pair < 2; ++pair) {
            const auto delta = timeline_cursor_delta(measure, pair);
            if (!delta) continue;
            const auto formatted = timeline_format(
                static_cast<double>(*delta), unit, native_us, hz);
            ImGui::PushID(pair);
            ImGui::TextColored(colors[static_cast<std::size_t>(pair * 2)],
                "%s delta: %s  (%llu %s)%s", pair ? "B1-B2" : "A1-A2",
                formatted.c_str(), static_cast<unsigned long long>(*delta),
                clock_unit(data.unit), hz ? " [manual estimate]" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit pair"))
                timeline_focus(timeline_viewport_,
                    std::min(measure.points[static_cast<std::size_t>(pair * 2)]->tick,
                             measure.points[static_cast<std::size_t>(pair * 2 + 1)]->tick),
                    std::max(measure.points[static_cast<std::size_t>(pair * 2)]->tick,
                             measure.points[static_cast<std::size_t>(pair * 2 + 1)]->tick));
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Copy measurements")) {
            std::string result;
            for (int pair = 0; pair < 2; ++pair) {
                if (!result.empty()) result += "\n";
                const int index = pair * 2;
                const auto &a = measure.points[static_cast<std::size_t>(index)];
                const auto &b = measure.points[static_cast<std::size_t>(index + 1)];
                result += pair ? "B1-B2" : "A1-A2";
                if (a && b) {
                    const auto delta = *timeline_cursor_delta(measure, pair);
                    result += ": " + std::to_string(a->tick) + " -> " +
                        std::to_string(b->tick) + " = " + std::to_string(delta) +
                        " " + clock_unit(data.unit) + " (" +
                        timeline_format(static_cast<double>(delta),
                                        unit, native_us, hz) + ")";
                } else {
                    result += ": incomplete";
                }
            }
            if (hz) result += "\nMANUAL CLOCK ESTIMATE / UNVERIFIED";
            ImGui::SetClipboardText(result.c_str());
        }
    }

    // Draw both vertical measurement cursors in every lane of this shared
    // viewport, including the ruler. The raw ticks never change with zoom.
    const auto draw_cursors = [&](ImDrawList *draw, const ImVec2 &p0,
                                  const ImVec2 &p1, bool on_ruler) {
        if (!measure.enabled) return;
        const auto x_at = [&](std::uint64_t tick) {
            const double offset = tick >= first ?
                static_cast<double>(tick - first) : 0.;
            return p0.x + static_cast<float>(
                ((offset - timeline_viewport_.start) / timeline_viewport_.width) *
                static_cast<double>(p1.x - p0.x));
        };
        draw->PushClipRect(p0, p1, true);
        if (measure.a && measure.b) {
            const float xa = x_at(measure.a->tick), xb = x_at(measure.b->tick);
            draw->AddRectFilled(
                ImVec2(std::min(xa, xb), p0.y),
                ImVec2(std::max(xa, xb), p1.y),
                IM_COL32(113, 184, 244, 23));
        }
        const auto one = [&](const std::optional<TimelineCursorPoint> &cursor,
                             ImU32 color, const char *letter) {
            if (!cursor) return;
            const float x = x_at(cursor->tick);
            if (x < p0.x || x > p1.x) return;
            draw->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), color, 2.f);
            if (on_ruler)
                draw->AddText(ImVec2(x + 3.f, p0.y + 1.f), color, letter);
        };
        one(measure.a, IM_COL32(255, 194, 80, 255), "A");
        one(measure.b, IM_COL32(65, 220, 255, 255), "B");
        draw->PopClipRect();
    };

    // All canvases share one viewport; never mutate the data's raw tick values.
    const auto interact = [&](float left, float width) {
        if (!ImGui::IsItemHovered() || width < 1.f) return;
        const auto &io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.f) {
            const double at = std::clamp(
                (static_cast<double>(ImGui::GetMousePos().x) - left) / width, 0., 1.);
            timeline_zoom(timeline_viewport_, std::pow(.8, io.MouseWheel), at);
        } else if (io.KeyShift && io.MouseWheel != 0.f) {
            timeline_pan(timeline_viewport_, -static_cast<double>(io.MouseWheel) * .2);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            timeline_pan(timeline_viewport_,
                         -static_cast<double>(io.MouseDelta.x) / width);
        if (!measure.enabled) return;
        const auto mouse_fraction = [&]() {
            return std::clamp(
                static_cast<double>(ImGui::GetMousePos().x - left) / width, 0., 1.);
        };
        const auto update_cursor = [&](int cursor) {
            const auto tick = timeline_tick_at_fraction(
                timeline_viewport_, mouse_fraction());
            auto point = measure.snap ?
                timeline_snap_tick(timeline_viewport_, tick, width,
                                   data.timeline_edges, data.timeline_events) :
                TimelineCursorPoint{tick, std::nullopt, false};
            timeline_set_cursor(measure, cursor, point);
        };
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            int chosen = measure.selected;
            // Picking an existing handle allows dragging A or B naturally.
            double distance = 9.;
            for (int i = 0; i < 2; ++i) {
                const auto &point = i == 0 ? measure.a : measure.b;
                if (!point) continue;
                const double x = left + (
                    (static_cast<double>(point->tick - first) -
                     timeline_viewport_.start) / timeline_viewport_.width) * width;
                const double d = std::abs(ImGui::GetMousePos().x - x);
                if (d < distance) { chosen = i; distance = d; }
            }
            timeline_drag_cursor_ = chosen;
            update_cursor(chosen);
        }
        if (ImGui::IsItemActive() && timeline_drag_cursor_ >= 0 &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
            update_cursor(timeline_drag_cursor_);
        if (ImGui::IsItemDeactivated())
            timeline_drag_cursor_ = -1;
    };
    const auto avail = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable("unified-timeline", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
            ImVec2(0.f, std::max(160.f, avail.y)))) {
        ImGui::TableSetupColumn("Thread / ISR / resource",
                                ImGuiTableColumnFlags_WidthStretch, .28f);
        ImGui::TableSetupColumn("Observed time intervals",
                                ImGuiTableColumnFlags_WidthStretch, .72f);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Time ruler");
        ImGui::TableNextColumn();
        const float ruler_width = std::max(50.f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("##timeline-ruler", ImVec2(ruler_width, 32.f));
        const auto ruler_start = ImGui::GetItemRectMin();
        const auto ruler_end = ImGui::GetItemRectMax();
        const auto divisions = ruler_width >= 950.f ? 6 : ruler_width >= 550.f ? 4 : 2;
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ruler_start, ruler_end, IM_COL32(20, 31, 47, 255));
        draw->PushClipRect(ruler_start, ruler_end, true);
        for (int i = 0; i <= divisions; ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(divisions);
            const float x = ruler_start.x + ruler_width * f;
            draw->AddLine(ImVec2(x, ruler_start.y), ImVec2(x, ruler_start.y + 7.f),
                          IM_COL32(165, 185, 205, 210));
            const auto caption = "+" + timeline_format(
                timeline_viewport_.start + timeline_viewport_.width * f,
                unit, native_us, hz);
            const auto size = ImGui::CalcTextSize(caption.c_str());
            const float caption_x = std::clamp(x - size.x * .5f,
                                              ruler_start.x,
                                              std::max(ruler_start.x, ruler_end.x - size.x));
            draw->AddText(ImVec2(caption_x, ruler_start.y + 10.f),
                          IM_COL32(200, 212, 229, 255), caption.c_str());
        }
        draw->PopClipRect();
        interact(ruler_start.x, ruler_width);
        draw_cursors(draw, ruler_start, ruler_end, true);

        std::size_t count = 0;
        for (const auto &[name, segments] : data.lane_index) {
            if (++count > 80) break;
            ImGui::PushID(static_cast<int>(count));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::TableNextColumn();
            const float width = std::max(50.f, ImGui::GetContentRegionAvail().x);
            const float height = std::max(26.f, ImGui::GetFrameHeight() + 5.f);
            ImGui::InvisibleButton("##span-canvas", ImVec2(width, height));
            const auto p0 = ImGui::GetItemRectMin();
            const auto p1 = ImGui::GetItemRectMax();
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const auto mouse = ImGui::GetMousePos();
            const double display_start = timeline_viewport_.start;
            const double display_width = timeline_viewport_.width;
            const double display_end = display_start + display_width;
            auto *lane_draw = ImGui::GetWindowDrawList();
            lane_draw->PushClipRect(p0, p1, true);
            lane_draw->AddRectFilled(ImVec2(p0.x, p0.y + 4.f),
                                     ImVec2(p1.x, p1.y - 4.f),
                                     IM_COL32(30, 42, 60, 255), 4.f);
            for (int mark = 1; mark < divisions; ++mark) {
                const float x = p0.x + width * static_cast<float>(mark) /
                                                 static_cast<float>(divisions);
                lane_draw->AddLine(ImVec2(x, p0.y + 4.f), ImVec2(x, p1.y - 4.f),
                                   IM_COL32(119, 140, 162, 60));
            }
            for (const auto index : segments) {
                if (index >= data.spans.size()) continue;
                const auto &span = data.spans[index];
                if (span.end < span.start || span.start < first || span.end < first)
                    continue;
                const double a = static_cast<double>(span.start - first);
                const double b = static_cast<double>(span.end - first);
                if (b < display_start || a > display_end) continue;
                const double left_fraction =
                    std::clamp((a - display_start) / display_width, 0., 1.);
                const double right_fraction =
                    std::clamp((b - display_start) / display_width, 0., 1.);
                const float left = p0.x + width * static_cast<float>(left_fraction);
                const float right = std::min(p1.x, std::max(left + 3.f,
                                    p0.x + width * static_cast<float>(right_fraction)));
                if (right <= left) continue;
                lane_draw->AddRectFilled(ImVec2(left, p0.y + 6.f),
                                         ImVec2(right, p1.y - 6.f),
                                         category_colour(span.category), 2.f);
                if (shared_selected_event_ && span.evidence_event &&
                    *shared_selected_event_ == *span.evidence_event)
                    lane_draw->AddRect(ImVec2(left, p0.y + 6.f),
                                       ImVec2(right, p1.y - 6.f),
                                       IM_COL32(255, 255, 255, 235), 2.f);
                if (!hovered || mouse.x < left || mouse.x > right) continue;
                const auto duration = span.end - span.start;
                const auto time = timeline_format(
                    static_cast<double>(duration), unit, native_us, hz);
                ImGui::SetTooltip("%s\n%s\nDuration: %s\nSource duration: %llu %s%s",
                                  span.lane.c_str(), span.category.c_str(),
                                  time.c_str(), static_cast<unsigned long long>(duration),
                                  clock_unit(data.unit),
                                  hz ? "\nManual Hz estimate, not verified" : "");
                if (clicked && !measure.enabled && span.evidence_event) {
                    shared_selected_event_ = *span.evidence_event;
                    page_ = Page::Evidence;
                }
            }
            lane_draw->PopClipRect();
            interact(p0.x, width);
            draw_cursors(lane_draw, p0, p1, false);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (data.lane_index.size() > 80 || data.records_limited)
        ImGui::TextColored(ImVec4(.99f, .77f, .48f, 1.f),
                           "Display capped: source evidence is larger than this view.");
}

void Workspace::draw_shared_graph() {
    const auto &data = unified_;
    if (!has_capability(data.capabilities, ToolCapability::Resources)) {
        ImGui::TextWrapped("This adapter does not identify resource relationships. "
                           "Raw numeric event IDs cannot be treated as kernel objects.");
        return;
    }
    ImGui::TextDisabled("Recorded object -> interval -> original event, only where linked.");
    ImGui::TextColored(ImVec4(.99f, .78f, .46f, 1.f),
                       "A visible link is evidence association, NOT proven causality.");
    const auto count = std::min<std::size_t>(data.cases.size(), 24);
    if (count == 0) {
        ImGui::TextDisabled("No observed complete resource intervals to connect.");
        return;
    }
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto em = ImGui::GetFontSize();
    for (std::size_t i = 0; i < count; ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto &item = data.cases[i];
        const bool linked = item.evidence_event &&
                            *item.evidence_event < data.events.size();
        if (width < em * 45.f) {
            ImGui::Separator();
            ImGui::TextWrapped("%s: %s", item.kind.c_str(), item.title.c_str());
            ImGui::ProgressBar(1.f, ImVec2(-1.f, em * 1.1f),
                               (std::to_string(item.end - item.start) +
                                " " + clock_unit(data.unit)).c_str());
            if (ImGui::Button("Inspect interval")) {
                shared_selected_case_ = i;
                page_ = Page::Resources;
            }
            if (linked) {
                ImGui::SameLine();
                if (ImGui::Button("Source event")) {
                    shared_selected_event_ = *item.evidence_event;
                    page_ = Page::Evidence;
                }
            }
        } else {
            const float h = std::max(50.f, em * 3.2f);
            ImGui::InvisibleButton("##relation", ImVec2(width, h));
            const auto p = ImGui::GetItemRectMin();
            const float pad = em * .42f;
            const float col = (width - em * 5.f) / 3.f;
            const float x0 = p.x;
            const float x1 = x0 + col + em * 2.5f;
            const float x2 = x1 + col + em * 2.5f;
            const float y0 = p.y + 2.f;
            const float y1 = p.y + h - 2.f;
            auto *draw = ImGui::GetWindowDrawList();
            const auto box = [&](float x, ImU32 colour, const std::string &title) {
                draw->AddRectFilled(ImVec2(x, y0), ImVec2(x + col, y1),
                                    colour, 6.f);
                draw->PushClipRect(ImVec2(x + pad, y0 + pad),
                                   ImVec2(x + col - pad, y1 - pad), true);
                draw->AddText(ImVec2(x + pad, y0 + em * .7f),
                              IM_COL32(242, 248, 255, 255), title.c_str());
                draw->PopClipRect();
            };
            box(x0, category_colour(item.kind), item.kind + ": " + item.title);
            box(x1, IM_COL32(49, 89, 119, 255),
                std::to_string(item.end - item.start) + " " + clock_unit(data.unit));
            box(x2, linked ? IM_COL32(50, 107, 96, 255) :
                             IM_COL32(54, 59, 70, 255),
                linked ? data.events[*item.evidence_event].title :
                         "No exact event link");
            draw->AddLine(ImVec2(x0 + col, p.y + h * .5f),
                          ImVec2(x1, p.y + h * .5f),
                          IM_COL32(126, 180, 222, 255), 2.f);
            if (linked)
                draw->AddLine(ImVec2(x1 + col, p.y + h * .5f),
                              ImVec2(x2, p.y + h * .5f),
                              IM_COL32(99, 193, 158, 255), 2.f);
            if (ImGui::IsItemHovered() && ImGui::IsItemClicked()) {
                const auto mouse = ImGui::GetMousePos();
                if (linked && mouse.x >= x2) {
                    shared_selected_event_ = *item.evidence_event;
                    page_ = Page::Evidence;
                } else {
                    shared_selected_case_ = i;
                    page_ = Page::Resources;
                }
            }
        }
        ImGui::PopID();
    }
    if (data.cases.size() > count)
        ImGui::TextDisabled("First 24 relationships shown. Resource list contains all cases.");
}

void Workspace::draw_shared_resources() {
    const auto &data = unified_;
    if (!has_capability(data.capabilities, ToolCapability::Resources)) {
        ImGui::TextWrapped("This adapter does not provide resource semantics. "
                           "Numeric event IDs alone cannot prove mutex, semaphore or queue state.");
        return;
    }
    if (data.objects.empty() && data.cases.empty()) {
        ImGui::TextDisabled("No resource evidence in this capture.");
        return;
    }
    if (!data.objects.empty()) {
        ImGui::SeparatorText("Observed kernel objects");
        const bool compact = ImGui::GetContentRegionAvail().x <
                             42.f * ImGui::GetFontSize();
        if (ImGui::BeginTable("unified-resource-objects", compact ? 2 : 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
            ImVec2(0.f, std::min(270.f, std::max(110.f,
                                               ImGui::GetContentRegionAvail().y * .30f))))) {
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, .55f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, .22f);
            if (!compact)
                ImGui::TableSetupColumn(
                    data.adapter == AdapterKind::ZephyrCtf ? "Matched cases" :
                    "Completed / unresolved", ImGuiTableColumnFlags_WidthStretch, .23f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(data.objects.size()));
            while (clip.Step()) {
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto &entry = data.objects[static_cast<std::size_t>(row)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", entry.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", entry.kind.c_str());
                    if (!compact) {
                        ImGui::TableNextColumn();
                        if (data.adapter == AdapterKind::ZephyrCtf)
                            ImGui::Text("%zu", entry.observed_events);
                        else
                            ImGui::Text("%zu / %zu", entry.completed_waits,
                                        entry.unresolved_waits);
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    if (data.cases.empty()) {
        ImGui::TextDisabled("No observed matched wait intervals; unresolved waits "
                            "must not be given fabricated durations.");
        return;
    }
    ImGui::SeparatorText("Observed wait intervals");
    const auto list = [&]() {
        std::uint64_t max_duration = 1;
        for (const auto &entry : data.cases)
            max_duration = std::max(max_duration, entry.end - entry.start);
        if (ImGui::BeginTable("unified-resource-list", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable,
                ImVec2(0.f, std::max(170.f, ImGui::GetContentRegionAvail().y)))) {
            ImGui::TableSetupColumn("Object and callback",
                                    ImGuiTableColumnFlags_WidthStretch, .65f);
            ImGui::TableSetupColumn("Observed interval",
                                    ImGuiTableColumnFlags_WidthStretch, .35f);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < data.cases.size(); ++i) {
                const auto &c = data.cases[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string label = c.kind + ": " + c.title;
                if (ImGui::Selectable(label.c_str(),
                    shared_selected_case_ && *shared_selected_case_ == i))
                    shared_selected_case_ = i;
                ImGui::TableNextColumn();
                const auto duration = c.end - c.start;
                ImGui::ProgressBar(static_cast<float>(duration) /
                                   static_cast<float>(max_duration),
                                   ImVec2(-1.f, 0.f),
                                   std::to_string(duration).c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%llu %s",
                        static_cast<unsigned long long>(duration),
                        clock_unit(data.unit));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };
    const auto inspector = [&]() {
        const auto index = shared_selected_case_.value_or(0);
        if (index >= data.cases.size()) return;
        const auto &chosen = data.cases[index];
        ImGui::SeparatorText("Selected observation");
        ImGui::TextWrapped("%s / %s", chosen.kind.c_str(), chosen.title.c_str());
        ImGui::Text("Duration: %llu %s",
                    static_cast<unsigned long long>(chosen.end - chosen.start),
                    clock_unit(data.unit));
        ImGui::TextDisabled("%s", chosen.completion_observed ?
                            "Observed matching completion" : "Completion not proven");
        ImGui::TextWrapped("%s", chosen.assessment.c_str());
        if (chosen.evidence_event && *chosen.evidence_event < data.events.size()) {
            if (ImGui::Button("Open recorded source evidence")) {
                shared_selected_event_ = *chosen.evidence_event;
                page_ = Page::Evidence;
            }
        } else {
            ImGui::TextDisabled("No exact source event linked by this adapter.");
        }
    };
    const auto layout = plan_layout(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize(), false);
    if (layout.show_two_columns &&
        ImGui::BeginTable("unified-resource-split", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Cases", ImGuiTableColumnFlags_WidthStretch, .56f);
        ImGui::TableSetupColumn("Evidence", ImGuiTableColumnFlags_WidthStretch, .44f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginChild("unified-case-list", ImVec2(0.f, std::max(180.f,
                           ImGui::GetContentRegionAvail().y)));
        list();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("unified-case-evidence", ImVec2(0.f, std::max(180.f,
                           ImGui::GetContentRegionAvail().y)));
        inspector();
        ImGui::EndChild();
        ImGui::EndTable();
    } else if (ImGui::BeginTabBar("unified-case-tabs")) {
        if (ImGui::BeginTabItem("Resource cases")) {
            list();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inspector")) {
            inspector();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void Workspace::draw_shared_events() {
    const auto &data = unified_;
    if (!has_capability(data.capabilities, ToolCapability::Events)) {
        ImGui::TextDisabled("No recorded events available.");
        return;
    }
    const auto &sorted = data.event_distribution;
    if (!sorted.empty()) {
        ImGui::SeparatorText("Recorded event categories");
        const auto peak = sorted.front().second;
        for (std::size_t i = 0; i < std::min<std::size_t>(sorted.size(), 4); ++i) {
            ImGui::TextUnformatted(sorted[i].first.c_str());
            ImGui::ProgressBar(static_cast<float>(sorted[i].second) /
                               static_cast<float>(peak), ImVec2(-1.f, 0.f),
                               std::to_string(sorted[i].second).c_str());
        }
    }
    ImGui::SeparatorText("Source events");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##unified-event-query", "Filter callback names",
                             shared_filter_.data(), shared_filter_.size());
    const auto query = ascii_lower(shared_filter_.data());
    if (query != shared_cached_filter_) {
        shared_cached_filter_ = query;
        shared_filtered_events_.clear();
        for (std::size_t i = 0; i < data.events.size(); ++i)
            if (query.empty() ||
                ascii_lower(data.events[i].title).find(query) != std::string::npos)
                shared_filtered_events_.push_back(i);
    }
    const auto &matches = shared_filtered_events_;
    if (ImGui::BeginTable("unified-event-browser", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
            ImVec2(0.f, std::max(180.f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthStretch, .10f);
        ImGui::TableSetupColumn("Callback / event", ImGuiTableColumnFlags_WidthStretch, .60f);
        ImGui::TableSetupColumn("Recorded tick", ImGuiTableColumnFlags_WidthStretch, .30f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matches.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i = matches[static_cast<std::size_t>(row)];
                const auto &event = data.events[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", event.source_index);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(event.title.c_str(),
                     shared_selected_event_ && *shared_selected_event_ == i,
                     ImGuiSelectableFlags_SpanAllColumns)) {
                    shared_selected_event_ = i;
                    page_ = Page::Evidence;
                }
                ImGui::TableNextColumn();
                if (event.epoch)
                    ImGui::Text("E%u / %llu", event.epoch,
                                static_cast<unsigned long long>(event.tick));
                else
                    ImGui::Text("%llu", static_cast<unsigned long long>(event.tick));
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (data.records_limited)
        ImGui::TextDisabled("Source event browser is bounded; evidence may be incomplete.");
}

void Workspace::draw_shared_findings() {
    const auto &data = unified_;
    if (data.findings.empty()) {
        ImGui::TextDisabled("No findings derived from recorded evidence.");
        return;
    }
    ImGui::TextDisabled("Findings are observations; they are not proven causes.");
    if (ImGui::BeginTable("unified-findings", 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
        ImVec2(0.f, std::max(180.f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("Finding", ImGuiTableColumnFlags_WidthStretch, .40f);
        ImGui::TableSetupColumn("Recorded assessment", ImGuiTableColumnFlags_WidthStretch, .60f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < data.findings.size(); ++i) {
            const auto &f = data.findings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(f.title.c_str(), false) && f.evidence_event) {
                shared_selected_event_ = *f.evidence_event;
                page_ = Page::Evidence;
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", f.detail.c_str());
            ImGui::TextDisabled("%s", f.confidence.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void Workspace::draw_shared_evidence() {
    const auto &data = unified_;
    ImGui::SeparatorText("Source identity and limits");
    ImGui::TextWrapped("%s", data.name.c_str());
    ImGui::TextDisabled("%s", data.source.c_str());
    ImGui::TextDisabled("%s", data.clock_label.c_str());
    ImGui::TextColored(ImVec4(.99f, .78f, .46f, 1.f),
                       "Clock calibrated: %s   |   Loss qualified: %s",
                       data.clock_calibrated ? "yes" : "no",
                       data.loss_qualified ? "yes" : "no");
    if (data.unit == ClockUnit::RawCtfTicks) {
        const bool clock_set =
            timeline_clock_hz_valid(timeline_clock_source_, timeline_manual_clock_enabled_,
                                    timeline_manual_clock_hz_);
        if (clock_set)
            ImGui::TextColored(ImVec4(.99f, .78f, .46f, 1.f),
                               "Manual clock: %s @ %llu Hz; display estimate only.",
                               timeline_clock_source_label(static_cast<TimelineClockSource>(
                                   timeline_clock_source_)),
                               static_cast<unsigned long long>(timeline_manual_clock_hz_));
        else
            ImGui::TextDisabled("No timestamp frequency configured; raw ticks only.");
        ImGui::TextDisabled("A/B measurements use raw ticks; no cross-capture sync assumed.");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Recorded source event");
    if (data.events.empty()) {
        ImGui::TextDisabled("No source events to inspect.");
        return;
    }
    const auto count = data.events.size();
    std::size_t selected = std::min(shared_selected_event_.value_or(0), count - 1);
    if (ImGui::Button("Previous") && selected > 0)
        --selected;
    ImGui::SameLine();
    if (ImGui::Button("Next") && selected + 1 < count)
        ++selected;
    shared_selected_event_ = selected;
    const auto &event = data.events[selected];
    ImGui::TextWrapped("%s", event.title.c_str());
    ImGui::Text("%llu %s", static_cast<unsigned long long>(event.tick),
                clock_unit(data.unit));
    if (event.epoch)
        ImGui::Text("Publisher epoch: %u", event.epoch);
    if (!event.actor.empty())
        ImGui::TextWrapped("Observed task: %s", event.actor.c_str());
    if (event.source_line)
        ImGui::Text("Source line: %zu", *event.source_line);
    if (event.source_byte)
        ImGui::Text("Source byte offset: %zu", *event.source_byte);
    if (ImGui::BeginTable("unified-evidence-fields", 2,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Recorded field", ImGuiTableColumnFlags_WidthStretch, .40f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, .60f);
        for (const auto &[name, value] : event.fields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", value.c_str());
        }
        ImGui::EndTable();
    }
}

void Workspace::draw_shared_tool(Page page) {
    if (unified_.adapter == AdapterKind::None) {
        ImGui::TextWrapped("Choose a supported RTOS data source, then Apply.");
        if (ImGui::Button("Choose data source..."))
            request_source(ImportAdapter::ZephyrCtf);
        return;
    }
    ImGui::TextColored(ImVec4(.77f, .90f, .99f, 1.f),
                       "%s", unified_.name.c_str());
    if (ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 30.f) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", unified_.clock_label.c_str());
    }
    if (ImGui::Button("Change source...")) {
        request_source(active_adapter_ == SourceAdapter::Ctf ? ImportAdapter::ZephyrCtf
                       : active_adapter_ == SourceAdapter::Live ? ImportAdapter::BinarySpool
                       : ImportAdapter::NormalizedTrace);
    }
    if (active_adapter_ == SourceAdapter::Ctf) {
        ImGui::SameLine();
        if (ImGui::Button("Refresh capture"))
            follow_ctf();
    }
    if (active_adapter_ == SourceAdapter::Live && !live_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, .58f, .48f, 1.f));
        ImGui::TextWrapped("Capture stopped: %s", live_error_.c_str());
        ImGui::PopStyleColor();
    }
    if (!unified_.evidence_warning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.98f, .77f, .48f, 1.f));
        ImGui::TextWrapped("%s", unified_.evidence_warning.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    switch (page) {
    case Page::Overview: case Page::Ctf: draw_shared_dashboard(); break;
    case Page::Timeline: case Page::CpuWaits:
    case Page::Diagnostics:
        if (page == Page::CpuWaits && !has_capability(
            unified_.capabilities, ToolCapability::QualifiedCpu))
            ImGui::TextDisabled("CPU utilization unavailable: insufficient qualified evidence.");
        if (page == Page::CpuWaits && has_capability(
            unified_.capabilities, ToolCapability::QualifiedCpu)) {
            for (const auto &[name, cpu] : unified_.qualified_cpu) {
                ImGui::TextUnformatted(name.c_str());
                ImGui::ProgressBar(std::clamp(static_cast<float>(cpu / 100.), 0.f, 1.f),
                                   ImVec2(-1.f, 0.f));
            }
        }
        draw_shared_timeline();
        break;
    case Page::Resources: draw_shared_resources(); break;
    case Page::Graph:
        draw_shared_graph();
        if (active_adapter_ == SourceAdapter::Normalized &&
            ImGui::CollapsingHeader("Advanced normalized causal graph"))
            draw_graph();
        break;
    case Page::Replay:
        ImGui::TextDisabled("Recorded event order only. No missing RTOS state inferred.");
        draw_shared_evidence();
        break;
    case Page::Causality:
        ImGui::TextDisabled("Correlation does not prove causality or deadlock.");
        draw_shared_graph();
        break;
    case Page::Events: draw_shared_events(); break;
    case Page::Findings: draw_shared_findings(); break;
    case Page::Evidence: draw_shared_evidence(); break;
    default: break;
    }
}
} // namespace observatory::desktop
