// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "observatory_build_info.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace observatory::desktop {
namespace {
std::filesystem::path file_path(const std::string& text) {
    // Preserve UTF-8 input semantics without deprecated C++20 std::filesystem::u8path.
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string as_text(std::uint64_t n) { return std::to_string(n); }
ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r,g,b,a); }
ImU32 state_colour(const std::string& state) {
    if (state == "running") return rgba(59,190,239);
    if (state == "idle") return rgba(66,190,157);
    if (state == "isr") return rgba(255,185,93);
    return rgba(114,128,150);
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
void label(const std::string& text) { ImGui::TextUnformatted(text.c_str()); }
void cell(const std::string& text) {
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(text.c_str());
}
void header(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    ImGui::Spacing();
}
constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_SizingStretchProp |
                                        ImGuiTableFlags_Resizable;
void small_metric(const char* title, const std::string& value) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", title);
    ImGui::SetWindowFontScale(1.40f);
    label(value);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();
}
void hover(const std::string& description) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", description.c_str());
}
} // namespace

Workspace::Workspace() {
    std::snprintf(path_.data(), path_.size(), "%s", "examples/timeline.trace");
    std::snprintf(export_path_.data(), export_path_.size(), "%s", "investigation.json");
}
void Workspace::recent(const std::string& path, const std::string& decoder, bool session) {
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
        [&](const Recent& r) { return r.path == path; }), recents_.end());
    recents_.insert(recents_.begin(), {path, decoder, session});
    if (recents_.size() > 8) recents_.resize(8);
}
void Workspace::fit() {
    if (!controller_.loaded()) return;
    begin_us_ = controller_.report().start_us;
    end_us_ = controller_.report().end_us;
    update_timeline();
}
void Workspace::update_timeline() {
    timeline_.reset();
    if (!controller_.loaded()) return;
    try {
        timeline_ = controller_.timeline(begin_us_, end_us_);
        notice_ = timeline_->warnings.empty() ? "Observed intervals only; no inferred RTOS states."
                                              : timeline_->warnings.front();
    } catch (const std::exception& e) {
        notice_ = e.what();
    }
}
bool Workspace::open_trace(const std::string& path, const std::string& decoder) {
    try {
        controller_.open_trace(file_path(path), decoder);
        error_.clear();
        std::snprintf(path_.data(), path_.size(), "%s", path.c_str());
        recent(path, decoder, false);
        fit();
        rebuild_event_filter();
        page_ = Page::Timeline;
        return true;
    } catch (const std::exception& e) { error_ = e.what(); return false; }
}
bool Workspace::open_session(const std::string& path) {
    try {
        controller_.open_session(file_path(path));
        error_.clear();
        std::snprintf(path_.data(), path_.size(), "%s", path.c_str());
        recent(path, "", true);
        fit();
        rebuild_event_filter();
        page_ = Page::Timeline;
        return true;
    } catch (const std::exception& e) { error_ = e.what(); return false; }
}
bool Workspace::export_report(const std::string& path) {
    try {
        controller_.export_report(file_path(path));
        error_.clear();
        notice_ = "Report exported to: " + path;
        return true;
    } catch (const std::exception& e) { error_ = e.what(); return false; }
}
void Workspace::accept_drop(const std::string& path) {
    if (std::filesystem::is_directory(file_path(path))) (void)open_session(path);
    else (void)open_trace(path, legacy_decoder_ ? "legacy-tsv" : "universal-v1");
}
void Workspace::rebuild_event_filter() {
    filtered_.clear();
    if (!controller_.loaded()) return;
    const auto& events = controller_.trace().capture.events;
    const auto query = lower(filter_.data());
    constexpr std::size_t max_preview = 20000;
    for (std::size_t i=0; i < std::min(events.size(), max_preview); ++i) {
        const auto& e=events[i];
        if (query.empty() || lower(e.task+" "+e.type+" "+e.object+" "+
                                    as_text(e.timestamp_us)).find(query) != std::string::npos)
            filtered_.push_back(i);
    }
}
void Workspace::zoom(bool in) {
    if (!controller_.loaded() || begin_us_>=end_us_) return;
    const auto& report = controller_.report();
    const auto width = end_us_ - begin_us_;
    const auto total = report.end_us - report.start_us;
    const auto next = in ? std::max<std::uint64_t>(1,width/2)
                         : width + std::min(width,total-width);
    const auto center = begin_us_ + width/2;
    auto start = center >= next/2 ? center-next/2 : report.start_us;
    if (start < report.start_us) start=report.start_us;
    if (next > report.end_us-start) start=report.end_us-next;
    begin_us_=start; end_us_=start+next;
    update_timeline();
}
void Workspace::draw_menus() {
    if(!ImGui::BeginMenuBar()) return;
    if(ImGui::BeginMenu("File")) {
        if(ImGui::MenuItem("Open trace...")) pending_open_=OpenDialog::Trace;
        if(ImGui::MenuItem("Open saved session...")) pending_open_=OpenDialog::Session;
        if(ImGui::BeginMenu("Recent files",!recents_.empty())) {
            for(std::size_t i=0;i<recents_.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto entry=recents_[i];
                const auto filename=std::filesystem::path(entry.path).filename().string();
                if(ImGui::MenuItem((filename.empty() ? entry.path : filename).c_str())) {
                    if(entry.session) (void)open_session(entry.path);
                    else (void)open_trace(entry.path,entry.decoder);
                }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",entry.path.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Export report...",nullptr,false,loaded()))
            export_dialog_requested_=true;
        ImGui::Separator();
        if(ImGui::MenuItem("Exit")) exit_requested_=true;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Settings")) {
        ImGui::MenuItem("Use legacy TSV format",nullptr,&legacy_decoder_);
        ImGui::MenuItem("Compact spacing",nullptr,&compact_spacing_);
        ImGui::TextDisabled("Display choices reset when the app closes");
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Show navigation",nullptr,&show_sidebar_);
        if(ImGui::BeginMenu("Go to panel")) {
            constexpr const char* names[]={"Overview","Execution timeline","CPU and waits",
                "Events","Findings","Evidence details"};
            for(int i=0;i<6;++i)
                if(ImGui::MenuItem(names[i],nullptr,static_cast<int>(page_)==i))
                    page_=static_cast<Page>(i);
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Reset layout")) {
            show_sidebar_=true;
            page_=Page::Overview;
            if(loaded()) fit();
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Tools")) {
        if(ImGui::MenuItem("Fit full timeline",nullptr,false,loaded())) {
            fit();
            page_=Page::Timeline;
        }
        if(ImGui::MenuItem("Zoom in",nullptr,false,loaded())) {
            zoom(true);
            page_=Page::Timeline;
        }
        if(ImGui::MenuItem("Zoom out",nullptr,false,loaded())) {
            zoom(false);
            page_=Page::Timeline;
        }
        ImGui::Separator();
        if(ImGui::MenuItem("CPU and wait analysis",nullptr,false,loaded())) page_=Page::CpuWaits;
        if(ImGui::MenuItem("Trace integrity",nullptr,false,loaded())) page_=Page::Evidence;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help")) {
        if(ImGui::MenuItem("About RTOS Observatory...")) show_about_=true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
void Workspace::draw_dialogs() {
    if(pending_open_!=OpenDialog::None) {
        active_open_=pending_open_;
        pending_open_=OpenDialog::None;
        ImGui::OpenPopup("Open evidence");
    }
    if(ImGui::BeginPopupModal("Open evidence",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(active_open_==OpenDialog::Trace ?
            "Open a local trace file" : "Open an indexed session folder");
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Path",path_.data(),path_.size());
        if(active_open_==OpenDialog::Trace) ImGui::Checkbox("Use legacy TSV format",&legacy_decoder_);
        if(ImGui::Button("Open")) {
            const bool ok=active_open_==OpenDialog::Session ?
                open_session(path_.data()) :
                open_trace(path_.data(),legacy_decoder_ ? "legacy-tsv" : "universal-v1");
            if(ok) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if(!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        ImGui::EndPopup();
    }
    if(export_dialog_requested_) {
        export_dialog_requested_=false;
        ImGui::OpenPopup("Export report");
    }
    if(ImGui::BeginPopupModal("Export report",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save an investigation report as JSON. Existing files will not be replaced.");
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Output file",export_path_.data(),export_path_.size());
        if(ImGui::Button("Export") && export_report(export_path_.data()))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if(!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        ImGui::EndPopup();
    }
    if(show_about_) {
        show_about_=false;
        ImGui::OpenPopup("About RTOS Observatory");
    }
    if(ImGui::BeginPopupModal("About RTOS Observatory",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("RTOS Observatory - offline RTOS evidence investigation");
        ImGui::Separator();
        ImGui::Text("Version: %s",observatory::build_info::version);
        ImGui::Text("Source commit: %s",observatory::build_info::commit);
        ImGui::Text("Build configured (UTC): %s",observatory::build_info::configured_utc);
        ImGui::Separator();
        ImGui::TextWrapped("C++20 / Dear ImGui / GLFW. Trace and session analysis only.");
        ImGui::TextWrapped("No live hardware probe, GDB connection, or native CTF decoder.");
        if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void Workspace::draw_sidebar() {
    const float footer_height=ImGui::GetTextLineHeightWithSpacing()+14.f;
    ImGui::BeginChild("Navigation", ImVec2(ImGui::GetFontSize()*17.f,-footer_height),
                      ImGuiChildFlags_Borders);
    ImGui::SetWindowFontScale(1.13f);
    ImGui::TextColored(ImVec4(.79f,.89f,1.f,1.f),"RTOS Observatory");
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled("Offline trace analysis");
    ImGui::Spacing();
    header("Open files");
    ImGui::TextDisabled("Trace file or saved session folder");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##input_path", path_.data(), path_.size());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enter a trace file or indexed-session directory, or drop one on the window.");
    ImGui::Checkbox("Use legacy TSV format", &legacy_decoder_);
    if (ImGui::Button("Open trace", ImVec2(-1, 0)))
        (void)open_trace(path_.data(), legacy_decoder_ ? "legacy-tsv" : "universal-v1");
    if (ImGui::Button("Open saved session", ImVec2(-1, 0)))
        (void)open_session(path_.data());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saved session directories must pass their existing integrity checks.");
    header("Explore");
    constexpr const char* names[] = {"Overview", "Execution timeline", "CPU and waits",
                                      "Events", "Findings", "Evidence details"};
    for (int i=0; i<6; ++i) {
        if (ImGui::Selectable(names[i],static_cast<int>(page_)==i,0,
                              ImVec2(0,ImGui::GetFrameHeight()+2.f)))
            page_=static_cast<Page>(i);
    }
    header("Recent files");
    for (std::size_t i=0; i<recents_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto r=recents_[i]; // reloading may mutate recents_ storage
        auto name=std::filesystem::path(r.path).filename().string();
        if (name.empty()) name=r.path;
        if (ImGui::Selectable(name.c_str(), false)) {
            if (r.session) (void)open_session(r.path);
            else (void)open_trace(r.path,r.decoder);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",r.path.c_str());
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Offline analysis only. Live capture is not connected.");
    ImGui::EndChild();
}
void Workspace::draw_overview() {
    if (!controller_.loaded()) { ImGui::TextDisabled("Open a trace or a saved session to start your investigation."); return; }
    const auto& report=controller_.report();
    const auto& trace=controller_.trace();
    if(ImGui::BeginTable("overview-metrics",3,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        small_metric("Recorded events",as_text(trace.capture.events.size()));
        ImGui::TableNextColumn();
        small_metric("Observed tasks",as_text(report.threads.size()));
        ImGui::TableNextColumn();
        small_metric("Findings",as_text(report.findings.size()));
        ImGui::EndTable();
    }
    header("Observed tasks");
    if (ImGui::BeginTable("tasks",4,table_flags,ImVec2(0,240))) {
        for (const auto* h: {"Task", "Priority", "Period (us)", "Idle"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto& task: report.threads) {
            ImGui::TableNextRow();
            cell(task.id);cell(task.priority ? std::to_string(*task.priority) : "Unknown");
            cell(task.period_us ? as_text(*task.period_us) : "Unknown");
            cell(task.idle ? "Yes" : "No");
        }
        ImGui::EndTable();
    }
    header("Trace integrity");
    const bool complete=report.complete && trace.evidence.sequence_available &&
                        !trace.evidence.missing_events && !trace.evidence.declared_drops;
    ImGui::TextColored(complete ? ImVec4(.35f,.85f,.67f,1) : ImVec4(1,.73f,.40f,1),
        "%s",complete ? "No reported sequence loss" : "Incomplete or unverified trace");
    for (const auto& warning: report.warnings) ImGui::BulletText("%s",warning.c_str());
    if (trace.evidence.missing_events)
        ImGui::BulletText("Sequence gaps: %llu",static_cast<unsigned long long>(trace.evidence.missing_events));
    if (trace.evidence.declared_drops)
        ImGui::BulletText("Declared drops: %llu",static_cast<unsigned long long>(trace.evidence.declared_drops));
}
void Workspace::draw_timeline() {
    if (!controller_.loaded()) { ImGui::TextDisabled("Open a trace to see the execution timeline."); return; }
    header("Execution timeline");
    ImGui::TextDisabled("Blue: task   |   Green: idle   |   Amber: interrupt   |   Grey: unknown   |   Purple: completed wait");
    ImGui::SetNextItemWidth(185);
    const bool start_changed=ImGui::InputScalar("From (us)",ImGuiDataType_U64,&begin_us_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(185);
    const bool end_changed=ImGui::InputScalar("To (us)",ImGuiDataType_U64,&end_us_);
    ImGui::SameLine();
    if (ImGui::Button("Apply") || start_changed || end_changed) update_timeline();
    ImGui::SameLine();
    if (ImGui::Button("Fit")) fit();
    ImGui::SameLine();
    if (ImGui::Button("Zoom +")) zoom(true);
    ImGui::SameLine();
    if (ImGui::Button("Zoom -")) zoom(false);
    if (!timeline_) { ImGui::TextColored(ImVec4(1,.65f,.45f,1),"%s",notice_.c_str()); return; }
    if (!timeline_->evidence_complete)
        ImGui::TextColored(ImVec4(1,.75f,.45f,1),"Incomplete trace: exact CPU percentages are unavailable");
    const auto& view=*timeline_;
    std::vector<std::pair<std::string,std::string>> rows;
    for (const auto& core:view.cores) rows.emplace_back("core",core.core);
    std::size_t task_count=0,wait_count=0;
    for (const auto& task:view.tasks) {
        if (task.running_us && task_count++<32) rows.emplace_back("task",task.task);
    }
    for (const auto& wait:view.waits) {
        if (wait_count>=32) break;
        const auto id=wait.task + " / " + wait.resource;
        const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
            return row.first=="wait" && row.second==id;
        });
        if(found==rows.end()) { rows.emplace_back("wait",id); ++wait_count; }
    }
    if (rows.empty()) ImGui::TextDisabled("No observed cores or completed wait intervals.");
    const float lane_height=33.f;
    const float total_height=std::max(230.f,74.f+lane_height*static_cast<float>(rows.size()));
    ImGui::BeginChild("timeline-scroll",ImVec2(0,0),ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_HorizontalScrollbar);
    const float width=std::max(680.f,ImGui::GetContentRegionAvail().x-10.f);
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline",ImVec2(width,total_height));
    auto* dl=ImGui::GetWindowDrawList();
    const float x0=origin.x+180.f, graph_width=width-198.f, y0=origin.y+40.f;
    const auto width_us=view.end_us-view.start_us;
    dl->PushClipRect(origin,ImVec2(origin.x+width,origin.y+total_height),true);
    for(int tick=0;tick<=4;++tick) {
        const float x=x0+graph_width*static_cast<float>(tick)/4.f;
        const auto t=view.start_us + static_cast<std::uint64_t>(
            static_cast<long double>(width_us)*tick/4);
        dl->AddLine(ImVec2(x,y0-8),ImVec2(x,y0+lane_height*static_cast<float>(rows.size())),
                    rgba(50,69,95));
        dl->AddText(ImVec2(x-36,y0-25),rgba(180,194,218),as_text(t).c_str());
    }
    for(std::size_t row=0;row<rows.size();++row) {
        const float y=y0+lane_height*static_cast<float>(row);
        dl->AddRectFilled(ImVec2(x0,y),ImVec2(x0+graph_width,y+29),
                          row%2 ? rgba(26,40,61) : rgba(31,47,69));
        const auto name=(rows[row].first=="core"?"CPU ":rows[row].first=="task"?"Task ":"Wait ")+rows[row].second;
        auto shown=name;
        if(shown.size()>24) shown=shown.substr(0,21)+"...";
        dl->AddText(ImVec2(origin.x+8.f,y+6.f),rgba(225,236,253),shown.c_str());
    }
    auto draw_bar=[&](std::size_t lane,std::uint64_t begin,std::uint64_t end,ImU32 colour,
                      const std::string& tooltip,bool unknown) {
        if(begin>=end || width_us==0) return;
        const float x1=x0+graph_width*static_cast<float>(
            static_cast<long double>(begin-view.start_us)/width_us);
        const float x2=x0+graph_width*static_cast<float>(
            static_cast<long double>(end-view.start_us)/width_us);
        if(x2<=x1) return;
        const float y=y0+lane_height*static_cast<float>(lane)+4.f;
        dl->AddRectFilled(ImVec2(x1,y),ImVec2(x2,y+21.f),colour,3.f);
        if(unknown) {
            for(float x=x1;x<x2 && x<x1+3000.f;x+=11.f)
                dl->AddLine(ImVec2(x,y+21.f),ImVec2(std::min(x+14.f,x2),y),rgba(173,188,207,160));
        }
        if(ImGui::IsMouseHoveringRect(ImVec2(x1,y),ImVec2(x2,y+21.f)))
            ImGui::SetTooltip("%s\n[%llu, %llu) us",tooltip.c_str(),
                static_cast<unsigned long long>(begin),
                static_cast<unsigned long long>(end));
    };
    for(const auto& s:view.slices) {
        auto it=std::find(rows.begin(),rows.end(),std::pair<std::string,std::string>{"core",s.core});
        if(it==rows.end()) continue;
        draw_bar(static_cast<std::size_t>(it-rows.begin()),s.start_us,s.end_us,
                 state_colour(s.state),"Core "+s.core+" / "+s.state+
                 (s.task.empty() ? "" : " / "+s.task),s.state=="unknown");
    }
    for(const auto& s:view.slices) {
        if(s.state!="running") continue;
        auto it=std::find(rows.begin(),rows.end(),std::pair<std::string,std::string>{"task",s.task});
        if(it==rows.end()) continue;
        draw_bar(static_cast<std::size_t>(it-rows.begin()),s.start_us,s.end_us,
                 rgba(65,144,232),"Observed execution: "+s.task+" on "+s.core,false);
    }
    for(const auto& w:view.waits) {
        auto it=std::find(rows.begin(),rows.end(),
                          std::pair<std::string,std::string>{"wait",w.task+" / "+w.resource});
        if(it==rows.end()) continue;
        draw_bar(static_cast<std::size_t>(it-rows.begin()),w.start_us,w.end_us,
                 rgba(165,138,251),"Completed "+w.kind+" wait / "+w.task+" / "+w.resource,false);
    }
    dl->PopClipRect();
    ImGui::EndChild();
    if(view.rendering_limited || task_count>32 || wait_count>=32)
        ImGui::TextDisabled("Display capped; all decoded intervals are retained in accounting.");
}
void Workspace::draw_cpu() {
    if(!timeline_) { ImGui::TextDisabled("Select a valid observed time window in Execution timeline."); return; }
    header("CPU time by core");
    ImGui::TextDisabled("Each core is reported separately. Unobserved time remains unknown.");
    if(ImGui::BeginTable("cpu",6,table_flags,ImVec2(0,230))) {
        for(const char* col: {"Core","Running (us)","ISR (us)","Idle (us)","Unknown (us)","Busy %"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for(const auto& c:timeline_->cores) {
            ImGui::TableNextRow();
            cell(c.core);cell(as_text(c.running_us));cell(as_text(c.isr_us));
            cell(as_text(c.idle_us));cell(as_text(c.unknown_us));
            cell(c.busy_percent ? std::to_string(*c.busy_percent)+"%" : "Unknown");
        }
        ImGui::EndTable();
    }
    header("Task activity and completed waits");
    if(ImGui::BeginTable("waits",4,table_flags,ImVec2(0,260))) {
        for(const char* col: {"Task","Running (us)","Completed wait (us)","Wait count"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        std::size_t count=0;
        for(const auto& t:timeline_->tasks) {
            if(!t.running_us && !t.completed_waits) continue;
            if(++count>2000) break;
            ImGui::TableNextRow();
            cell(t.task);cell(as_text(t.running_us));
            cell(as_text(t.completed_wait_us));cell(as_text(t.completed_waits));
        }
        ImGui::EndTable();
    }
    for(const auto& warning:timeline_->warnings)
        ImGui::TextColored(ImVec4(1.f,.75f,.42f,1),"%s",warning.c_str());
    ImGui::TextDisabled("Busy includes ISR time; unknown scheduler time is never assumed idle.");
}
void Workspace::draw_events() {
    if(!controller_.loaded()) return;
    header("Recorded events");
    if(ImGui::InputText("Filter type, task, object or time",filter_.data(),filter_.size()))
        rebuild_event_filter();
    ImGui::TextDisabled("Displaying %zu rows; preview capped at first 20,000 events.",
                        filtered_.size());
    if(ImGui::BeginTable("events",6,table_flags,ImVec2(0,-1))) {
        for(const char* col: {"Time (us)","Type","Task","Object / core","Value","Line"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        const auto& events=controller_.trace().capture.events;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_.size()));
        while(clipper.Step())
            for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                const auto& e=events[filtered_[static_cast<std::size_t>(i)]];
                ImGui::TableNextRow();
                cell(as_text(e.timestamp_us));cell(e.type);cell(e.task);cell(e.object);
                cell(std::to_string(e.value));cell(std::to_string(e.line));
            }
        ImGui::EndTable();
    }
}
void Workspace::draw_findings() {
    if(!controller_.loaded()) return;
    header("Observed findings");
    ImGui::TextDisabled("Patterns in the trace are not confirmed root causes.");
    if(ImGui::BeginTable("findings",6,table_flags,ImVec2(0,-1))) {
        for(const char* col: {"Code","Confidence","Task","Resource","Observed (us)","Description"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for(const auto& f:controller_.report().findings) {
            ImGui::TableNextRow();
            cell(f.code);cell(f.confidence);cell(f.task);cell(f.resource);
            cell(as_text(f.observed_us));cell(f.description);
        }
        ImGui::EndTable();
    }
}
void Workspace::draw_evidence() {
    if(!controller_.loaded()) return;
    const auto& decoded=controller_.trace();
    const auto& evidence=decoded.evidence;
    header("Trace source and integrity");
    for(const auto& [name,value]:std::vector<std::pair<std::string,std::string>>{
        {"Path",controller_.origin().string()},
        {"Mode",controller_.mode()},
        {"Decoder",decoded.decoder},
        {"Firmware label (not attested)",decoded.identity.firmware},
        {"RTOS label",decoded.identity.rtos},
        {"Clock label",decoded.identity.clock},
        {"Ticks per second",as_text(decoded.identity.ticks_per_second)},
        {"Sequence available",evidence.sequence_available ? "yes" : "no"},
        {"Missing events",as_text(evidence.missing_events)},
        {"Declared drops",as_text(evidence.declared_drops)},
        {"Analyzer complete",controller_.report().complete ? "yes" : "no"}
    }) {
        ImGui::TextDisabled("%s",name.c_str());
        ImGui::SameLine(270);
        label(value);
    }
    header("Export investigation report");
    ImGui::SetNextItemWidth(540);
    ImGui::InputText("##export",export_path_.data(),export_path_.size());
    ImGui::SameLine();
    if(ImGui::Button("Export JSON report")) (void)export_report(export_path_.data());
    ImGui::TextDisabled("Existing files and immutable session directories cannot be overwritten.");
    ImGui::TextDisabled("Offline normalized evidence only. No live target connection or raw CTF adapter.");
}
void Workspace::draw() {
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if(compact_spacing_) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(6.f,4.f));
    ImGui::Begin("RTOS Observatory",nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar);
    draw_menus();
    if(show_sidebar_) {
        draw_sidebar();
        ImGui::SameLine();
    }
    const float footer_height=ImGui::GetTextLineHeightWithSpacing()+14.f;
    ImGui::BeginChild("Investigation",ImVec2(0,-footer_height),ImGuiChildFlags_None);
    ImGui::SetWindowFontScale(1.18f);
    ImGui::TextColored(ImVec4(.92f,.96f,1.f,1.f),"Trace investigation");
    ImGui::SetWindowFontScale(1.f);
    if(controller_.loaded()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Session: %s",controller_.report().session.c_str());
    }
    ImGui::Separator();
    if(!error_.empty()) {
        ImGui::TextColored(ImVec4(1,.53f,.40f,1),"ERROR: %s",error_.c_str());
        ImGui::SameLine();
        if(ImGui::SmallButton("Dismiss")) error_.clear();
    } else if(!notice_.empty()) {
        ImGui::TextDisabled("%s",notice_.c_str());
    }
    switch(page_) {
      case Page::Overview: draw_overview();break;
      case Page::Timeline: draw_timeline();break;
      case Page::CpuWaits: draw_cpu();break;
      case Page::Events: draw_events();break;
      case Page::Findings: draw_findings();break;
      case Page::Evidence: draw_evidence();break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled("v%s   |   Source %s   |   Configured %s",
        observatory::build_info::version,observatory::build_info::commit,
        observatory::build_info::configured_utc);
    draw_dialogs();
    ImGui::End();
    if(compact_spacing_) ImGui::PopStyleVar();
}
} // namespace observatory::desktop
