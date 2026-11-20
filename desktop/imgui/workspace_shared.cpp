// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
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
    ImGui::SeparatorText("Observed callback categories");
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
    if (data.spans.empty()) {
        ImGui::TextDisabled("No recorded complete spans. Missing transitions are not inferred.");
        return;
    }
    std::uint64_t first = data.spans.front().start;
    std::uint64_t last = data.spans.front().end;
    const auto &lanes = data.lane_index; // indexed once when capture loads
    for (const auto &span : data.spans) {
        if (span.end < span.start) continue;
        first = std::min(first, span.start);
        last = std::max(last, span.end);
    }
    if (lanes.empty()) {
        ImGui::TextDisabled("No valid intervals.");
        return;
    }
    const double range = last > first ? static_cast<double>(last - first) : 1.;
    ImGui::TextDisabled("Intervals measured in %s. Only recorded spans are drawn.",
                        clock_unit(data.unit));
    ImGui::Text("Window: %llu - %llu",
                static_cast<unsigned long long>(first),
                static_cast<unsigned long long>(last));
    const auto avail = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable("unified-timeline", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
            ImVec2(0.f, std::max(160.f, avail.y)))) {
        ImGui::TableSetupColumn("Thread / ISR / resource",
                                ImGuiTableColumnFlags_WidthStretch, .33f);
        ImGui::TableSetupColumn("Observed time intervals",
                                ImGuiTableColumnFlags_WidthStretch, .67f);
        ImGui::TableHeadersRow();
        std::size_t count = 0;
        for (const auto &[name, segments] : lanes) {
            if (++count > 80) break;
            ImGui::PushID(static_cast<int>(count));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::TableNextColumn();
            const auto width = std::max(50.f, ImGui::GetContentRegionAvail().x);
            const float height = std::max(26.f, ImGui::GetFrameHeight() + 5.f);
            ImGui::InvisibleButton("##span-canvas", ImVec2(width, height));
            const auto p0 = ImGui::GetItemRectMin();
            const auto p1 = ImGui::GetItemRectMax();
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ImVec2(p0.x, p0.y + 4.f),
                                ImVec2(p1.x, p1.y - 4.f),
                                IM_COL32(30, 42, 60, 255), 4.f);
            for (int mark = 1; mark < 5; ++mark) {
                const float x = p0.x + width * static_cast<float>(mark) / 5.f;
                draw->AddLine(ImVec2(x, p0.y + 4.f), ImVec2(x, p1.y - 4.f),
                              IM_COL32(119, 140, 162, 60));
            }
            const auto x_position = [&](std::uint64_t tick) {
                const auto offset = tick >= first ? tick - first : 0;
                return p0.x + static_cast<float>(static_cast<double>(offset) / range) * width;
            };
            for (const auto index : segments) {
                const auto *s = &data.spans[index];
                const auto left = x_position(s->start);
                const auto right = std::min(p1.x, std::max(left + 3.f, x_position(s->end)));
                draw->AddRectFilled(ImVec2(left, p0.y + 6.f),
                                    ImVec2(right, p1.y - 6.f),
                                    category_colour(s->category), 2.f);
                if (!ImGui::IsItemHovered()) continue;
                const auto mouse = ImGui::GetMousePos();
                if (mouse.x < left || mouse.x > right) continue;
                ImGui::SetTooltip("%s\n%llu %s\n%s",
                                  s->lane.c_str(),
                                  static_cast<unsigned long long>(s->end - s->start),
                                  clock_unit(data.unit), s->category.c_str());
                if (ImGui::IsItemClicked() && s->evidence_event) {
                    shared_selected_event_ = *s->evidence_event;
                    page_ = Page::Evidence;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (lanes.size() > 80 || data.records_limited)
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
                ImGui::TableSetupColumn("Completed / unresolved",
                                        ImGuiTableColumnFlags_WidthStretch, .23f);
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
