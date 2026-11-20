// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "imgui.h"
#include "observatory_build_info.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#endif

namespace observatory::desktop {
namespace {
std::filesystem::path file_path(const std::string &text) {
    // Preserve UTF-8 input semantics without deprecated C++20 std::filesystem::u8path.
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

#ifdef _WIN32
std::optional<std::string> choose_native_file_or_folder(bool folder) {
    const auto init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return std::nullopt;
    IFileOpenDialog *dlg = nullptr;
    std::optional<std::string> result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg))) &&
        dlg) {
        DWORD flags{};
        if (SUCCEEDED(dlg->GetOptions(&flags))) {
            const DWORD selection = folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
            (void)dlg->SetOptions(flags | selection | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        (void)dlg->SetTitle(folder ? L"Select RTOS Observatory evidence folder"
                                   : L"Select RTOS Observatory trace file");
        if (SUCCEEDED(dlg->Show(GetForegroundWindow()))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR wide = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide) {
                    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                                                             -1, nullptr, 0, nullptr, nullptr);
                    if (required > 1 && required < 8192) {
                        std::string utf8(static_cast<std::size_t>(required), '\0');
                        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                                utf8.data(), required, nullptr, nullptr) > 0) {
                            utf8.pop_back(); // terminal NUL
                            result = std::move(utf8);
                        }
                    }
                    CoTaskMemFree(wide);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return result;
}
std::optional<std::string> choose_ctf_directory_native() {
    return choose_native_file_or_folder(true);
}
#endif

bool is_ctf_root(const std::filesystem::path &path) {
    std::error_code ec;
    if (std::filesystem::is_symlink(path, ec) || !std::filesystem::is_directory(path, ec))
        return false;
    for (const auto *filename : {"metadata", "channel0_0"}) {
        const auto file = path / filename;
        if (std::filesystem::is_symlink(file, ec) || !std::filesystem::is_regular_file(file, ec))
            return false;
    }
    return true;
}
std::filesystem::path resolve_ctf_root(const std::filesystem::path &input) {
    std::error_code ec;
    if (input.empty() || std::filesystem::is_symlink(input, ec) ||
        !std::filesystem::is_directory(input, ec))
        throw observatory::Error(
            "Capture folder not found. Use Browse to select an existing folder.");
    if (is_ctf_root(input))
        return input;
    // Bounded discovery of one nested level, never follow symbolic links or
    // silently guess when more than one capture is present.
    std::optional<std::filesystem::path> candidate;
    std::filesystem::directory_iterator it(
        input, std::filesystem::directory_options::skip_permission_denied, ec),
        end;
    if (ec)
        throw observatory::Error("Cannot inspect selected folder (permission denied).");
    std::size_t visited = 0;
    for (; it != end && visited < 128; it.increment(ec), ++visited) {
        if (ec)
            break;
        if (!it->is_symlink(ec) && it->is_directory(ec) && is_ctf_root(it->path())) {
            if (candidate)
                throw observatory::Error(
                    "Multiple CTF captures found. Browse to the exact capture folder.");
            candidate = it->path();
        }
    }
    if (ec || it != end)
        throw observatory::Error("CTF folder scan incomplete. Select the exact capture folder.");
    if (candidate)
        return *candidate;
    const auto meta = input / "metadata", channel = input / "channel0_0";
    if (!std::filesystem::is_regular_file(meta, ec) &&
        !std::filesystem::is_regular_file(channel, ec))
        throw observatory::Error(
            "CTF files not found. Select the folder containing BOTH 'metadata' and 'channel0_0'.");
    if (!std::filesystem::is_regular_file(meta, ec))
        throw observatory::Error("Missing CTF metadata file in selected folder.");
    throw observatory::Error("Missing channel0_0 data file in selected folder.");
}

std::string as_text(std::uint64_t n) { return std::to_string(n); }
ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImU32 state_colour(const std::string &state) {
    if (state == "running")
        return rgba(59, 190, 239);
    if (state == "idle")
        return rgba(66, 190, 157);
    if (state == "isr")
        return rgba(255, 185, 93);
    return rgba(114, 128, 150);
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
void label(const std::string &text) { ImGui::TextUnformatted(text.c_str()); }
void cell(const std::string &text) {
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(text.c_str());
}
void header(const char *title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    ImGui::Spacing();
}
constexpr ImGuiTableFlags table_flags =
    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
void small_metric(const char *title, const std::string &value) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", title);
    ImGui::SetWindowFontScale(1.40f);
    label(value);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndGroup();
}
void hover(const std::string &description) {
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", description.c_str());
}
} // namespace

Workspace::Workspace() {
    std::snprintf(path_.data(), path_.size(), "%s", "examples/timeline.trace");
    std::snprintf(export_path_.data(), export_path_.size(), "%s", "investigation.json");
    std::snprintf(fault_path_.data(), fault_path_.size(), "%s", "examples/fault-snapshot.tsv");
    std::snprintf(symbol_manifest_path_.data(), symbol_manifest_path_.size(), "%s", "symbols.tsv");
    std::snprintf(symbol_elf_path_.data(), symbol_elf_path_.size(), "%s", "firmware.elf");
    std::snprintf(symbol_source_root_.data(), symbol_source_root_.size(), "%s", ".");
    std::snprintf(bridge_file_path_.data(), bridge_file_path_.size(), "%s",
                  "rtos-observatory-dsf-status.tsv");
    std::snprintf(snapshot_receipt_path_.data(), snapshot_receipt_path_.size(), "%s",
                  "examples/approved-snapshot.tsv");
    std::snprintf(live_file_path_.data(), live_file_path_.size(), "%s", "capture.rtob");
}
void Workspace::recent(const std::string &path, const std::string &decoder, bool session) {
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
                                  [&](const Recent &r) { return r.path == path; }),
                   recents_.end());
    recents_.insert(recents_.begin(), {path, decoder, session});
    if (recents_.size() > 8)
        recents_.resize(8);
}
void Workspace::fit() {
    if (!controller_.loaded())
        return;
    begin_us_ = controller_.report().start_us;
    end_us_ = controller_.report().end_us;
    update_timeline();
}
void Workspace::update_graph() {
    graph_.reset();
    if (!controller_.loaded())
        return;
    try {
        graph_ = build_causal_graph(controller_.trace(), begin_us_, end_us_, graph_filter_.data(),
                                    {}, graph_event_boundary_);
        if (std::none_of(graph_->nodes.begin(), graph_->nodes.end(),
                         [&](const GraphNode &node) { return node.id == graph_selected_; }))
            graph_selected_.clear();
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
void Workspace::update_diagnostics() {
    diagnostics_.reset();
    if (!controller_.loaded())
        return;
    try {
        diagnostics_ =
            analyze_scheduling(controller_.trace(), controller_.report(), relative_deadlines_);
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
void Workspace::update_resources() {
    resources_.reset();
    if (!controller_.loaded())
        return;
    try {
        resources_ =
            analyze_resource_observations(controller_.trace(), controller_.report().complete);
        if (!resource_selected_.empty()) {
            const auto found = std::find_if(
                resources_->resources.begin(), resources_->resources.end(),
                [&](const ResourceSummary &entry) { return entry.name == resource_selected_; });
            if (found == resources_->resources.end())
                resource_selected_.clear();
        }
        if (resource_selected_.empty() && !resources_->resources.empty())
            resource_selected_ = resources_->resources.front().name;
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
void Workspace::update_causality() {
    causality_.reset();
    selected_causal_finding_ = 0;
    if (!controller_.loaded())
        return;
    try {
        causality_ = analyze_causal_patterns(controller_.trace(), controller_.report(),
                                             long_mutex_wait_threshold_us_);
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
void Workspace::update_replay() {
    replay_.reset();
    if (!controller_.loaded())
        return;
    try {
        const auto &events = controller_.trace().capture.events;
        if (!events.empty()) {
            const auto capped_count = std::min<std::size_t>(events.size(), 100000);
            replay_cursor_ = std::min<std::uint64_t>(replay_cursor_, capped_count - 1);
        }
        replay_ = replay_at(controller_.trace(), static_cast<std::size_t>(replay_cursor_));
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
void Workspace::update_timeline() {
    timeline_.reset();
    update_graph();
    if (!controller_.loaded())
        return;
    try {
        timeline_ = controller_.timeline(begin_us_, end_us_);
        notice_ = timeline_->warnings.empty() ? "Observed intervals only; no inferred RTOS states."
                                              : timeline_->warnings.front();
    } catch (const std::exception &e) {
        notice_ = e.what();
    }
}
bool Workspace::open_trace(const std::string &path, const std::string &decoder) {
    try {
        controller_.open_trace(file_path(path), decoder);
        focused_event_.reset();
        graph_selected_.clear();
        graph_event_boundary_.reset();
        replay_cursor_ =
            controller_.trace().capture.events.empty()
                ? 0
                : static_cast<std::uint64_t>(
                      std::min<std::size_t>(controller_.trace().capture.events.size(), 100000) - 1);
        relative_deadlines_.clear();
        deadline_task_.fill(0);
        update_replay();
        update_diagnostics();
        resource_selected_.clear();
        update_resources();
        long_mutex_wait_threshold_us_ = 0;
        update_causality();
        error_.clear();
        std::snprintf(path_.data(), path_.size(), "%s", path.c_str());
        recent(path, decoder, false);
        fit();
        rebuild_event_filter();
        active_adapter_ = SourceAdapter::Normalized;
        refresh_unified_view();
        page_ = Page::Overview;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_session(const std::string &path) {
    try {
        controller_.open_session(file_path(path));
        focused_event_.reset();
        graph_selected_.clear();
        graph_event_boundary_.reset();
        replay_cursor_ =
            controller_.trace().capture.events.empty()
                ? 0
                : static_cast<std::uint64_t>(
                      std::min<std::size_t>(controller_.trace().capture.events.size(), 100000) - 1);
        relative_deadlines_.clear();
        deadline_task_.fill(0);
        update_replay();
        update_diagnostics();
        resource_selected_.clear();
        update_resources();
        long_mutex_wait_threshold_us_ = 0;
        update_causality();
        error_.clear();
        std::snprintf(path_.data(), path_.size(), "%s", path.c_str());
        recent(path, "", true);
        fit();
        rebuild_event_filter();
        active_adapter_ = SourceAdapter::Normalized;
        refresh_unified_view();
        page_ = Page::Overview;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_fault_snapshot(const std::string &path) {
    try {
        std::ifstream input(file_path(path), std::ios::binary);
        if (!input)
            throw Error("Cannot open local fault snapshot");
        auto next_snapshot = parse_fault_snapshot(input);
        auto next_view = analyze_fault_snapshot(next_snapshot);
        fault_snapshot_ = std::move(next_snapshot);
        fault_view_ = std::move(next_view);
        fault_selected_finding_ = 0;
        fault_origin_ = path;
        std::snprintf(fault_path_.data(), fault_path_.size(), "%s", path.c_str());
        error_.clear();
        page_ = Page::Faults;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_source_index(const std::string &manifest, const std::string &elf,
                                  const std::string &source_root) {
    try {
        std::ifstream file(file_path(manifest), std::ios::binary);
        if (!file)
            throw Error("Cannot open local symbol/source sidecar");
        auto next = parse_source_index(file);
        auto verified = verify_source_index(next, file_path(elf), file_path(source_root));
        symbols_ = std::move(next);
        symbol_verification_ = std::move(verified);
        selected_symbol_ = 0;
        source_excerpt_.reset();
        address_lookup_ = 0;
        std::snprintf(symbol_manifest_path_.data(), symbol_manifest_path_.size(), "%s",
                      manifest.c_str());
        std::snprintf(symbol_elf_path_.data(), symbol_elf_path_.size(), "%s", elf.c_str());
        std::snprintf(symbol_source_root_.data(), symbol_source_root_.size(), "%s",
                      source_root.c_str());
        error_.clear();
        page_ = Page::SourceGraph;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_bridge_status(const std::string &path) {
    try {
        const auto snapshot = read_bridge_file(file_path(path));
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        if (now < 0)
            throw Error("Local clock predates Unix epoch");
        auto observed = bridge_monitor_.observe(snapshot, static_cast<std::uint64_t>(now));
        bridge_observation_ = std::move(observed);
        if (!bridge_observation_->accepted) {
            error_ = "W14 lifecycle publication rejected: " + bridge_observation_->status;
            page_ = Page::DebugBridge;
            return false;
        }
        std::snprintf(bridge_file_path_.data(), bridge_file_path_.size(), "%s", path.c_str());
        error_.clear();
        page_ = Page::DebugBridge;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_snapshot_receipt(const std::string &path) {
    try {
        auto receipt = read_snapshot_receipt(file_path(path));
        snapshot_receipt_ = std::move(receipt);
        std::snprintf(snapshot_receipt_path_.data(), snapshot_receipt_path_.size(), "%s",
                      path.c_str());
        page_ = Page::Snapshot;
        error_.clear();
        return true;
    } catch (const std::exception &e) {
        snapshot_receipt_.reset(); // never show a previously accepted receipt for a failed import
        error_ = e.what();
        return false;
    }
}
bool Workspace::open_live_file(const std::string &path) {
    try {
        const auto file = file_path(path);
        if (path.empty() || std::filesystem::is_symlink(file) ||
            !std::filesystem::is_regular_file(file))
            throw Error("W18 requires an existing local, non-symlink binary spool");
        if (std::filesystem::file_size(file) > 64u * 1024u * 1024u)
            throw Error("W18 spool exceeds 64 MiB bounded session");
        std::optional<LiveSession> fresh(std::in_place);
        live_session_ = std::move(fresh);
        live_file_offset_ = 0;
        live_active_file_ = path;
        live_follow_ = true;
        live_error_.clear();
        std::snprintf(live_file_path_.data(), live_file_path_.size(), "%s", path.c_str());
        page_ = Page::LiveTrace;
        const bool ok = poll_live_file();
        if (ok) {
            active_adapter_ = SourceAdapter::Live;
            refresh_unified_view();
            page_ = Page::Overview;
        } else {
            live_session_.reset();
            live_follow_ = false;
            if (active_adapter_ == SourceAdapter::Live) {
                active_adapter_ = SourceAdapter::None;
                unified_ = ToolSummary{}; // no stale evidence after failed replacement
            }
        }
        return ok;
    } catch (const std::exception &e) {
        live_error_ = e.what();
        live_follow_ = false;
        live_session_.reset();
        if (active_adapter_ == SourceAdapter::Live) {
            active_adapter_ = SourceAdapter::None;
            unified_ = ToolSummary{};
        }
        return false;
    }
}
bool Workspace::poll_live_file() {
    if (!live_session_)
        return false;
    try {
        const auto file = file_path(live_active_file_);
        if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file))
            throw Error("W18 spool disappeared or became a symlink");
        const auto total = std::filesystem::file_size(file);
        if (total < live_file_offset_)
            throw Error("W18 spool truncated/replaced: stop and open a new epoch capture");
        if (total == live_file_offset_)
            return true;
        constexpr std::uint64_t max_spool = 64u * 1024u * 1024u;
        if (total > max_spool)
            throw Error("W18 spool exceeded 64 MiB");
        std::ifstream input(file, std::ios::binary);
        if (!input)
            throw Error("W18 spool unavailable");
        input.seekg(static_cast<std::streamoff>(live_file_offset_));
        if (!input)
            throw Error("W18 spool seek failed");
        std::array<std::uint8_t, 16384> buffer{};
        const auto desired = std::min<std::uint64_t>(total - live_file_offset_, buffer.size());
        input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(desired));
        const auto received = input.gcount();
        if (received != static_cast<std::streamsize>(desired))
            throw Error("W18 spool changed during bounded read");
        live_session_->feed(buffer.data(), static_cast<std::size_t>(received));
        live_file_offset_ += static_cast<std::uint64_t>(received);
        if (active_adapter_ == SourceAdapter::Live)
            refresh_unified_view();
        return true;
    } catch (const std::exception &e) {
        live_error_ = e.what();
        live_follow_ = false;
        return false;
    }
}
bool Workspace::export_report(const std::string &path) {
    try {
        controller_.export_report(file_path(path));
        error_.clear();
        notice_ = "Report exported to: " + path;
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
void Workspace::accept_drop(const std::string &path) {
    if (std::filesystem::is_directory(file_path(path)) &&
        std::filesystem::is_regular_file(file_path(path) / "metadata") &&
        std::filesystem::is_regular_file(file_path(path) / "channel0_0"))
        (void)open_ctf_directory(path);
    else if (path.ends_with(".rtob"))
        (void)open_live_file(path);
    else if (path.ends_with(".fault.tsv") || path.ends_with("fault-snapshot.tsv"))
        (void)open_fault_snapshot(path);
    else if (std::filesystem::is_directory(file_path(path)))
        (void)open_session(path);
    else
        (void)open_trace(path, legacy_decoder_ ? "legacy-tsv" : "universal-v1");
}
void Workspace::rebuild_event_filter() {
    filtered_.clear();
    if (!controller_.loaded())
        return;
    const auto &events = controller_.trace().capture.events;
    if (focused_event_ && *focused_event_ < events.size()) {
        filtered_.push_back(*focused_event_);
        return;
    }
    const auto query = lower(filter_.data());
    constexpr std::size_t max_preview = 20000;
    for (std::size_t i = 0; i < std::min(events.size(), max_preview); ++i) {
        const auto &e = events[i];
        if (query.empty() ||
            lower(e.task + " " + e.type + " " + e.object + " " + as_text(e.timestamp_us))
                    .find(query) != std::string::npos)
            filtered_.push_back(i);
    }
}
void Workspace::zoom(bool in) {
    if (!controller_.loaded() || begin_us_ >= end_us_)
        return;
    const auto &report = controller_.report();
    const auto width = end_us_ - begin_us_;
    const auto total = report.end_us - report.start_us;
    const auto next =
        in ? std::max<std::uint64_t>(1, width / 2) : width + std::min(width, total - width);
    const auto center = begin_us_ + width / 2;
    auto start = center >= next / 2 ? center - next / 2 : report.start_us;
    if (start < report.start_us)
        start = report.start_us;
    if (next > report.end_us - start)
        start = report.end_us - next;
    begin_us_ = start;
    end_us_ = start + next;
    update_timeline();
}
void Workspace::refresh_unified_view() {
    // All adapters project into a single read-only GUI contract. Original
    // time units are retained, and no unsupported capability is asserted.
    switch (active_adapter_) {
    case SourceAdapter::Normalized:
        if (controller_.loaded()) {
            const auto &decoded = controller_.trace();
            const auto &report = controller_.report();
            const bool qualified = timeline_ && timeline_->evidence_complete &&
                                   report.complete &&
                                   decoded.evidence.sequence_available &&
                                   decoded.evidence.missing_events == 0 &&
                                   decoded.evidence.declared_drops == 0;
            unified_ = project_normalized(decoded.capture, report,
                          timeline_ ? &*timeline_ : nullptr, qualified,
                          resources_ ? &*resources_ : nullptr);
        } else unified_ = ToolSummary{};
        break;
    case SourceAdapter::Ctf:
        unified_ = ctf_ ? project_ctf(*ctf_,
                 ctf_analysis_ ? &*ctf_analysis_ : nullptr,
                 file_path(ctf_active_directory_).filename().string()) : ToolSummary{};
        break;
    case SourceAdapter::Live:
        unified_ = live_session_ ? project_spool(*live_session_) : ToolSummary{};
        break;
    case SourceAdapter::None:
        unified_ = ToolSummary{};
        break;
    }
    shared_selected_event_.reset();
    shared_selected_case_.reset();
    shared_cached_filter_ = "\x01"; // force refresh even if query is empty
    shared_filtered_events_.clear();
}

void Workspace::draw_menus() {
    if (!ImGui::BeginMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
#ifdef _WIN32
        if (ImGui::MenuItem("Import trace file..."))
            request_source(ImportAdapter::NormalizedTrace);
        if (ImGui::MenuItem("Import saved session..."))
            request_source(ImportAdapter::SavedSession);
#endif
        if (ImGui::MenuItem("Open trace..."))
            request_source(ImportAdapter::NormalizedTrace);
        if (ImGui::MenuItem("Open saved session..."))
            request_source(ImportAdapter::SavedSession);
        if (ImGui::MenuItem("Open Zephyr CTF folder..."))
            request_source(ImportAdapter::ZephyrCtf);
        if (ImGui::MenuItem("Open fault snapshot..."))
            page_ = Page::Faults;
        if (ImGui::MenuItem("Open C/C++/Rust symbol index..."))
            page_ = Page::SourceGraph;
        if (ImGui::MenuItem("Inspect Eclipse DSF bridge status..."))
            page_ = Page::DebugBridge;
        if (ImGui::MenuItem("Open approved snapshot receipt..."))
            page_ = Page::Snapshot;
        if (ImGui::MenuItem("Follow W18 binary trace spool..."))
            request_source(ImportAdapter::BinarySpool);
        if (ImGui::BeginMenu("Recent files", !recents_.empty())) {
            for (std::size_t i = 0; i < recents_.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto entry = recents_[i];
                const auto filename = std::filesystem::path(entry.path).filename().string();
                if (ImGui::MenuItem((filename.empty() ? entry.path : filename).c_str())) {
                    if (entry.session)
                        (void)open_session(entry.path);
                    else
                        (void)open_trace(entry.path, entry.decoder);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", entry.path.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export report...", nullptr, false,
                            active_adapter_ == SourceAdapter::Normalized && loaded()))
            export_dialog_requested_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            exit_requested_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
        ImGui::MenuItem("Use legacy TSV format", nullptr, &legacy_decoder_);
        ImGui::MenuItem("Compact spacing", nullptr, &compact_spacing_);
        ImGui::SeparatorText("ACCESSIBILITY / FONT");
        ImGui::SliderFloat("Text + controls", &ui_scale_, 1.0f, 2.2f, "%.2fx");
        if (ImGui::MenuItem("Comfortable text (120%)"))
            ui_scale_ = 1.2f;
        if (ImGui::MenuItem("Large text (150%)"))
            ui_scale_ = 1.5f;
        if (ImGui::MenuItem("Very large text (180%)"))
            ui_scale_ = 1.8f;
        if (ImGui::MenuItem("Default text size (100%)"))
            ui_scale_ = 1.f;
        ImGui::TextDisabled("Use Ctrl+Plus / Ctrl+Minus to zoom.");
        ImGui::TextDisabled("Window resize and display scaling are automatic");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Show navigation", nullptr, &show_sidebar_);
        const bool can_detach = page_ != Page::Ctf && detached_analysis_pages_.size() < 6;
        if (ImGui::MenuItem("Detach current analysis", nullptr, false, can_detach)) {
            if (std::find(detached_analysis_pages_.begin(), detached_analysis_pages_.end(),
                          page_) == detached_analysis_pages_.end())
                detached_analysis_pages_.push_back(page_);
        }
        ImGui::TextDisabled("Drag detached windows outside to another monitor.");
        ImGui::Separator();
        ImGui::SeparatorText("Detachable common tools");
        const bool has_evidence = unified_.adapter != AdapterKind::None;
        if (ImGui::MenuItem("Detach resource explorer", nullptr,
                            &ctf_explorer_detached_, has_evidence))
            page_ = Page::Resources;
        if (ImGui::MenuItem("Detach evidence inspector", nullptr,
                            &ctf_inspector_detached_, has_evidence))
            page_ = Page::Evidence;
        if (ImGui::MenuItem("Detach scheduling timeline", nullptr,
                            &ctf_scheduler_detached_, has_evidence))
            page_ = Page::Timeline;
        if (ImGui::MenuItem("Close detached common tools", nullptr, false,
                            ctf_explorer_detached_ || ctf_inspector_detached_ ||
                            ctf_scheduler_detached_)) {
            ctf_explorer_detached_ = false;
            ctf_inspector_detached_ = false;
            ctf_scheduler_detached_ = false;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Go to panel")) {
            constexpr const char *names[] = {"Overview",
                                             "Execution timeline",
                                             "CPU and waits",
                                             "Causal graph",
                                             "Time-travel replay",
                                             "Scheduling diagnostics",
                                             "Resources and waits",
                                             "Causal diagnostics",
                                             "Fault & memory",
                                             "Source/symbol graph",
                                             "Debugger lifecycle",
                                             "Approved snapshot receipts",
                                             "Live trace epochs",
                                             "Events",
                                             "Findings",
                                             "Evidence details"};
            for (int i = 0; i < 16; ++i)
                if (ImGui::MenuItem(names[i], nullptr, static_cast<int>(page_) == i))
                    page_ = static_cast<Page>(i);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset layout")) {
            detached_analysis_pages_.clear();
            ctf_explorer_detached_ = false;
            ctf_inspector_detached_ = false;
            ctf_scheduler_detached_ = false;
            show_sidebar_ = true;
            page_ = Page::Overview;
            if (loaded())
                fit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        const bool shared_loaded = loaded() ||
            (active_adapter_ == SourceAdapter::Ctf && ctf_.has_value());
        if (ImGui::MenuItem("Fit full timeline", nullptr, false, active_adapter_ == SourceAdapter::Normalized && loaded())) {
            fit();
            page_ = Page::Timeline;
        }
        if (ImGui::MenuItem("Zoom in", nullptr, false, active_adapter_ == SourceAdapter::Normalized && loaded())) {
            zoom(true);
            page_ = Page::Timeline;
        }
        if (ImGui::MenuItem("Zoom out", nullptr, false, active_adapter_ == SourceAdapter::Normalized && loaded())) {
            zoom(false);
            page_ = Page::Timeline;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("CPU and wait analysis", nullptr, false, shared_loaded))
            page_ = Page::CpuWaits;
        if (ImGui::MenuItem("Causal graph studio", nullptr, false, shared_loaded))
            page_ = Page::Graph;
        if (ImGui::MenuItem("Time-travel replay", nullptr, false, shared_loaded))
            page_ = Page::Replay;
        if (ImGui::MenuItem("Scheduling diagnostics", nullptr, false, shared_loaded))
            page_ = Page::Diagnostics;
        if (ImGui::MenuItem("Resources and waits", nullptr, false, shared_loaded))
            page_ = Page::Resources;
        if (ImGui::MenuItem("Causal diagnostics", nullptr, false, shared_loaded))
            page_ = Page::Causality;
        if (ImGui::MenuItem("Fault & memory snapshots"))
            page_ = Page::Faults;
        if (ImGui::MenuItem("Source/symbol graph"))
            page_ = Page::SourceGraph;
        if (ImGui::MenuItem("Debugger lifecycle (passive)"))
            page_ = Page::DebugBridge;
        if (ImGui::MenuItem("Snapshot receipts (read-only)"))
            page_ = Page::Snapshot;
        if (ImGui::MenuItem("Live trace epochs (binary spool)"))
            page_ = Page::LiveTrace;
        if (ImGui::MenuItem("CTF resource diagnostics", nullptr, false,
                            active_adapter_ == SourceAdapter::Ctf))
            page_ = Page::Resources;
        if (ImGui::MenuItem("Trace integrity", nullptr, false, shared_loaded))
            page_ = Page::Evidence;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About RTOS Observatory..."))
            show_about_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
void Workspace::draw_dialogs() {
    if (pending_open_ != OpenDialog::None) {
        active_open_ = pending_open_;
        pending_open_ = OpenDialog::None;
        ImGui::OpenPopup("Open evidence");
    }
    if (ImGui::BeginPopupModal("Open evidence", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(active_open_ == OpenDialog::Trace
                                   ? "Open a local trace file"
                                   : "Open an indexed session folder");
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Path", path_.data(), path_.size());
        if (active_open_ == OpenDialog::Trace)
            ImGui::Checkbox("Use legacy TSV format", &legacy_decoder_);
        if (ImGui::Button("Open")) {
            const bool ok =
                active_open_ == OpenDialog::Session
                    ? open_session(path_.data())
                    : open_trace(path_.data(), legacy_decoder_ ? "legacy-tsv" : "universal-v1");
            if (ok)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndPopup();
    }
    if (export_dialog_requested_) {
        export_dialog_requested_ = false;
        ImGui::OpenPopup("Export report");
    }
    if (ImGui::BeginPopupModal("Export report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Save an investigation report as JSON. Existing files will not be replaced.");
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Output file", export_path_.data(), export_path_.size());
        if (ImGui::Button("Export") && export_report(export_path_.data()))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndPopup();
    }
    if (show_about_) {
        show_about_ = false;
        ImGui::OpenPopup("About RTOS Observatory");
    }
    if (ImGui::BeginPopupModal("About RTOS Observatory", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("RTOS Observatory - offline RTOS evidence investigation");
        ImGui::TextUnformatted("Author: Niwantha Meepage");
        ImGui::Separator();
        ImGui::Text("Version: %s", observatory::build_info::version);
        ImGui::Text("Source commit: %s", observatory::build_info::commit);
        ImGui::Text("Build configured (UTC): %s", observatory::build_info::configured_utc);
        ImGui::Separator();
        ImGui::TextWrapped("C++20 / Dear ImGui / GLFW. Trace and session analysis only.");
        ImGui::TextWrapped("No live hardware probe, GDB connection, or native CTF decoder.");
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    draw_source_dialog();
}
void Workspace::request_source(ImportAdapter adapter) {
    import_adapter_ = adapter;
    import_path_.fill(0);
    const char *previous = "";
    switch (adapter) {
    case ImportAdapter::ZephyrCtf: previous = ctf_directory_.data(); break;
    case ImportAdapter::NormalizedTrace: previous = path_.data(); break;
    case ImportAdapter::SavedSession: break;
    case ImportAdapter::BinarySpool: previous = live_file_path_.data(); break;
    }
    std::snprintf(import_path_.data(), import_path_.size(), "%s", previous);
    source_dialog_requested_ = true;
    error_.clear();
    ctf_error_.clear();
}

void Workspace::draw_source_dialog() {
    if (source_dialog_requested_) {
        source_dialog_requested_ = false;
        ImGui::OpenPopup("Connect data source");
    }
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        ImVec2(std::clamp(viewport->WorkSize.x * .65f, 380.f, 700.f), 0.f),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Connect data source", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextColored(ImVec4(.72f, .87f, 1.f, 1.f), "OPEN LOCAL EVIDENCE");
    ImGui::TextWrapped("Choose an adapter, select a file or folder, then Apply.");
    const char *options[] = {"Zephyr CTF", "Universal trace", "Saved session",
                             "Binary spool"};
    int chosen = static_cast<int>(import_adapter_);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##adapter", &chosen, options, 4)) {
        import_adapter_ = static_cast<ImportAdapter>(chosen);
        import_path_.fill(0);
    }
    const bool folder = import_adapter_ == ImportAdapter::ZephyrCtf ||
                        import_adapter_ == ImportAdapter::SavedSession;
    ImGui::Spacing();
    if (import_adapter_ == ImportAdapter::ZephyrCtf)
        ImGui::TextWrapped("Select a folder containing metadata and channel0_0.");
    else if (import_adapter_ == ImportAdapter::SavedSession)
        ImGui::TextWrapped("Select a saved Observatory session folder.");
    else if (import_adapter_ == ImportAdapter::BinarySpool)
        ImGui::TextWrapped("Select a local .rtob event spool. Raw epochs only.");
    else
        ImGui::TextWrapped("Select a supported normalized .trace file.");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##source-path", folder ? "Folder" : "File",
                             import_path_.data(), import_path_.size());
#ifdef _WIN32
    if (ImGui::Button(folder ? "Browse folder..." : "Browse file...")) {
        if (auto selected = choose_native_file_or_folder(folder))
            std::snprintf(import_path_.data(), import_path_.size(), "%s",
                          selected->c_str());
    }
    ImGui::SameLine();
#endif
    if (import_adapter_ == ImportAdapter::NormalizedTrace)
        ImGui::Checkbox("Legacy TSV", &legacy_decoder_);
    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(115.f, 0.f))) {
        bool opened = false;
        const std::string selected(import_path_.data());
        switch (import_adapter_) {
        case ImportAdapter::ZephyrCtf: opened = open_ctf_directory(selected); break;
        case ImportAdapter::NormalizedTrace:
            opened = open_trace(selected, legacy_decoder_ ? "legacy-tsv" : "universal-v1");
            break;
        case ImportAdapter::SavedSession: opened = open_session(selected); break;
        case ImportAdapter::BinarySpool: opened = open_live_file(selected); break;
        }
        if (opened)
            ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(115.f, 0.f)))
        ImGui::CloseCurrentPopup();
    const auto &message = import_adapter_ == ImportAdapter::ZephyrCtf
                              ? ctf_error_
                              : import_adapter_ == ImportAdapter::BinarySpool
                                  ? live_error_ : error_;
    if (!message.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.f, .59f, .44f, 1.f), "Cannot open source");
        ImGui::TextWrapped("%s", message.c_str());
    }
    ImGui::EndPopup();
}

void Workspace::draw_sidebar() {
    const float footer_height = ImGui::GetTextLineHeightWithSpacing() + 14.f;
    const float width = plan_layout(ImGui::GetContentRegionAvail().x,
                                    ImGui::GetFontSize()).sidebar_width;
    ImGui::BeginChild("Navigation", ImVec2(width, -footer_height),
                      ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(.75f, .87f, .99f, 1.f), "RTOS OBSERVATORY");
    ImGui::TextDisabled("One workspace / shared tools");
    ImGui::SeparatorText("DATA SOURCES");
    const auto source = [this](ImportAdapter adapter, const char *title,
                               SourceAdapter active) {
        ImGui::PushID(static_cast<int>(adapter));
        if (ImGui::Selectable(title, active_adapter_ == active))
            request_source(adapter);
        ImGui::PopID();
    };
    source(ImportAdapter::ZephyrCtf, "Zephyr CTF", SourceAdapter::Ctf);
    source(ImportAdapter::NormalizedTrace, "Universal trace", SourceAdapter::Normalized);
    source(ImportAdapter::SavedSession, "Saved session", SourceAdapter::Normalized);
    source(ImportAdapter::BinarySpool, "Binary event spool", SourceAdapter::Live);
    ImGui::Spacing();
    ImGui::SeparatorText("COMMON TOOLS");
    const auto nav = [this](Page destination, const char *title) {
        if (ImGui::Selectable(title, page_ == destination))
            page_ = destination;
    };
    nav(Page::Overview, "Dashboard");
    nav(Page::Timeline, "Graphical timeline");
    nav(Page::CpuWaits, "CPU / scheduling");
    nav(Page::Resources, "Mutex / semaphore / queue");
    nav(Page::Graph, "Evidence relationships");
    nav(Page::Diagnostics, "Scheduling / ISR");
    nav(Page::Events, "Event explorer");
    nav(Page::Findings, "Findings");
    nav(Page::Evidence, "Evidence inspector");
    nav(Page::Replay, "Time-travel / event order");
    nav(Page::Causality, "Causal diagnostics");
    nav(Page::Faults, "Fault and memory");
    nav(Page::SourceGraph, "Source and symbols");
    nav(Page::DebugBridge, "Debugger lifecycle");
    nav(Page::Snapshot, "Snapshot receipts");
    nav(Page::LiveTrace, "Live event epochs");
    if (active_adapter_ == SourceAdapter::Ctf && !ctf_active_directory_.empty()) {
        ImGui::SeparatorText("ACTIVE CAPTURE");
        const auto name = file_path(ctf_active_directory_).filename().string();
        ImGui::TextWrapped("%s", name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", ctf_active_directory_.c_str());
    }
    ImGui::EndChild();
}

void Workspace::draw_overview() {
    if (!controller_.loaded()) {
        ImGui::TextDisabled("Open a trace or a saved session to start your investigation.");
        return;
    }
    const auto &report = controller_.report();
    const auto &trace = controller_.trace();
    if (ImGui::BeginTable("overview-metrics", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        small_metric("Recorded events", as_text(trace.capture.events.size()));
        ImGui::TableNextColumn();
        small_metric("Observed tasks", as_text(report.threads.size()));
        ImGui::TableNextColumn();
        small_metric("Findings", as_text(report.findings.size()));
        ImGui::EndTable();
    }
    header("Observed tasks");
    if (ImGui::BeginTable("tasks", 4, table_flags, ImVec2(0, 240))) {
        for (const auto *h : {"Task", "Priority", "Period (us)", "Idle"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto &task : report.threads) {
            ImGui::TableNextRow();
            cell(task.id);
            cell(task.priority ? std::to_string(*task.priority) : "Unknown");
            cell(task.period_us ? as_text(*task.period_us) : "Unknown");
            cell(task.idle ? "Yes" : "No");
        }
        ImGui::EndTable();
    }
    header("Trace integrity");
    const bool complete = report.complete && trace.evidence.sequence_available &&
                          !trace.evidence.missing_events && !trace.evidence.declared_drops;
    ImGui::TextColored(complete ? ImVec4(.35f, .85f, .67f, 1) : ImVec4(1, .73f, .40f, 1), "%s",
                       complete ? "No reported sequence loss" : "Incomplete or unverified trace");
    for (const auto &warning : report.warnings)
        ImGui::BulletText("%s", warning.c_str());
    if (trace.evidence.missing_events)
        ImGui::BulletText("Sequence gaps: %llu",
                          static_cast<unsigned long long>(trace.evidence.missing_events));
    if (trace.evidence.declared_drops)
        ImGui::BulletText("Declared drops: %llu",
                          static_cast<unsigned long long>(trace.evidence.declared_drops));
}
void Workspace::draw_timeline() {
    if (!controller_.loaded()) {
        ImGui::TextDisabled("Open a trace to see the execution timeline.");
        return;
    }
    header("Execution timeline");
    ImGui::TextDisabled("Blue: task   |   Green: idle   |   Amber: interrupt   |   Grey: unknown   "
                        "|   Purple: completed wait");
    ImGui::SetNextItemWidth(185);
    const bool start_changed = ImGui::InputScalar("From (us)", ImGuiDataType_U64, &begin_us_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(185);
    const bool end_changed = ImGui::InputScalar("To (us)", ImGuiDataType_U64, &end_us_);
    ImGui::SameLine();
    if (ImGui::Button("Apply") || start_changed || end_changed)
        update_timeline();
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
        fit();
    ImGui::SameLine();
    if (ImGui::Button("Zoom +"))
        zoom(true);
    ImGui::SameLine();
    if (ImGui::Button("Zoom -"))
        zoom(false);
    if (!timeline_) {
        ImGui::TextColored(ImVec4(1, .65f, .45f, 1), "%s", notice_.c_str());
        return;
    }
    if (!timeline_->evidence_complete)
        ImGui::TextColored(ImVec4(1, .75f, .45f, 1),
                           "Incomplete trace: exact CPU percentages are unavailable");
    const auto &view = *timeline_;
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto &core : view.cores)
        rows.emplace_back("core", core.core);
    std::size_t task_count = 0, wait_count = 0;
    for (const auto &task : view.tasks) {
        if (task.running_us && task_count++ < 32)
            rows.emplace_back("task", task.task);
    }
    for (const auto &wait : view.waits) {
        if (wait_count >= 32)
            break;
        const auto id = wait.task + " / " + wait.resource;
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
            return row.first == "wait" && row.second == id;
        });
        if (found == rows.end()) {
            rows.emplace_back("wait", id);
            ++wait_count;
        }
    }
    if (rows.empty())
        ImGui::TextDisabled("No observed cores or completed wait intervals.");
    const float lane_height = 33.f;
    const float total_height =
        std::max(230.f, 74.f + lane_height * static_cast<float>(rows.size()));
    ImGui::BeginChild("timeline-scroll", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const float width = std::max(680.f, ImGui::GetContentRegionAvail().x - 10.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline", ImVec2(width, total_height));
    auto *dl = ImGui::GetWindowDrawList();
    const float x0 = origin.x + 180.f, graph_width = width - 198.f, y0 = origin.y + 40.f;
    const auto width_us = view.end_us - view.start_us;
    dl->PushClipRect(origin, ImVec2(origin.x + width, origin.y + total_height), true);
    for (int tick = 0; tick <= 4; ++tick) {
        const float x = x0 + graph_width * static_cast<float>(tick) / 4.f;
        const auto t = view.start_us +
                       static_cast<std::uint64_t>(static_cast<long double>(width_us) * tick / 4);
        dl->AddLine(ImVec2(x, y0 - 8),
                    ImVec2(x, y0 + lane_height * static_cast<float>(rows.size())),
                    rgba(50, 69, 95));
        dl->AddText(ImVec2(x - 36, y0 - 25), rgba(180, 194, 218), as_text(t).c_str());
    }
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const float y = y0 + lane_height * static_cast<float>(row);
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + graph_width, y + 29),
                          row % 2 ? rgba(26, 40, 61) : rgba(31, 47, 69));
        const auto name = (rows[row].first == "core"   ? "CPU "
                           : rows[row].first == "task" ? "Task "
                                                       : "Wait ") +
                          rows[row].second;
        auto shown = name;
        if (shown.size() > 24)
            shown = shown.substr(0, 21) + "...";
        dl->AddText(ImVec2(origin.x + 8.f, y + 6.f), rgba(225, 236, 253), shown.c_str());
    }
    auto draw_bar = [&](std::size_t lane, std::uint64_t begin, std::uint64_t end, ImU32 colour,
                        const std::string &tooltip, bool unknown) {
        if (begin >= end || width_us == 0)
            return;
        const float x1 =
            x0 + graph_width *
                     static_cast<float>(static_cast<long double>(begin - view.start_us) / width_us);
        const float x2 =
            x0 + graph_width *
                     static_cast<float>(static_cast<long double>(end - view.start_us) / width_us);
        if (x2 <= x1)
            return;
        const float y = y0 + lane_height * static_cast<float>(lane) + 4.f;
        dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + 21.f), colour, 3.f);
        if (unknown) {
            for (float x = x1; x < x2 && x < x1 + 3000.f; x += 11.f)
                dl->AddLine(ImVec2(x, y + 21.f), ImVec2(std::min(x + 14.f, x2), y),
                            rgba(173, 188, 207, 160));
        }
        if (ImGui::IsMouseHoveringRect(ImVec2(x1, y), ImVec2(x2, y + 21.f)))
            ImGui::SetTooltip("%s\n[%llu, %llu) us", tooltip.c_str(),
                              static_cast<unsigned long long>(begin),
                              static_cast<unsigned long long>(end));
    };
    for (const auto &s : view.slices) {
        auto it = std::find(rows.begin(), rows.end(),
                            std::pair<std::string, std::string>{"core", s.core});
        if (it == rows.end())
            continue;
        draw_bar(static_cast<std::size_t>(it - rows.begin()), s.start_us, s.end_us,
                 state_colour(s.state),
                 "Core " + s.core + " / " + s.state + (s.task.empty() ? "" : " / " + s.task),
                 s.state == "unknown");
    }
    for (const auto &s : view.slices) {
        if (s.state != "running")
            continue;
        auto it = std::find(rows.begin(), rows.end(),
                            std::pair<std::string, std::string>{"task", s.task});
        if (it == rows.end())
            continue;
        draw_bar(static_cast<std::size_t>(it - rows.begin()), s.start_us, s.end_us,
                 rgba(65, 144, 232), "Observed execution: " + s.task + " on " + s.core, false);
    }
    for (const auto &w : view.waits) {
        auto it =
            std::find(rows.begin(), rows.end(),
                      std::pair<std::string, std::string>{"wait", w.task + " / " + w.resource});
        if (it == rows.end())
            continue;
        draw_bar(static_cast<std::size_t>(it - rows.begin()), w.start_us, w.end_us,
                 rgba(165, 138, 251),
                 "Completed " + w.kind + " wait / " + w.task + " / " + w.resource, false);
    }
    dl->PopClipRect();
    ImGui::EndChild();
    if (view.rendering_limited || task_count > 32 || wait_count >= 32)
        ImGui::TextDisabled("Display capped; all decoded intervals are retained in accounting.");
}
void Workspace::draw_graph() {
    if (!controller_.loaded()) {
        ImGui::TextDisabled("Open an offline trace to explore recorded task/resource links.");
        return;
    }
    header("Causal graph studio");
    ImGui::TextDisabled("Solid arrows: observed events. Thin links: source evidence. Correlation "
                        "does not prove cause.");
    if (ImGui::InputText("Task filter", graph_filter_.data(), graph_filter_.size()))
        update_graph();
    ImGui::SameLine();
    ImGui::Checkbox("Show evidence", &graph_show_evidence_);
    ImGui::SameLine();
    if (ImGui::Button("Reset graph view")) {
        graph_scale_ = 1.0f;
        graph_pan_x_ = 0.f;
        graph_pan_y_ = 0.f;
    }
    if (!graph_) {
        ImGui::TextDisabled("No graph is available for this time window.");
        return;
    }
    const auto &view = *graph_;
    ImGui::Text("Window [%llu, %llu] us | %zu nodes | %zu links | Clock: %s",
                static_cast<unsigned long long>(view.begin_us),
                static_cast<unsigned long long>(view.end_us), view.nodes.size(), view.edges.size(),
                view.clock_domain.c_str());
    if (!view.evidence_complete || view.truncated)
        ImGui::TextColored(ImVec4(1.f, .75f, .42f, 1.f),
                           "INCOMPLETE / CAPPED: unobserved states and relations remain unknown");
    for (const auto &warning : view.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    if (graph_event_boundary_) {
        ImGui::Text("Replayed only through recorded event #%zu", *graph_event_boundary_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Show all events in time range")) {
            graph_event_boundary_.reset();
            update_graph();
        }
    }
    ImGui::TextDisabled(
        "Drag with middle mouse to pan; mouse wheel to zoom; click a node for linked events.");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(std::max(500.f, ImGui::GetContentRegionAvail().x), 450.f);
    ImGui::InvisibleButton("##causal-graph-canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    auto &io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        graph_pan_x_ += io.MouseDelta.x;
        graph_pan_y_ += io.MouseDelta.y;
    }
    if (hovered && io.MouseWheel != 0.f)
        graph_scale_ = std::clamp(graph_scale_ * (io.MouseWheel > 0 ? 1.12f : .89f), .35f, 2.50f);
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 canvas_end(origin.x + canvas_size.x, origin.y + canvas_size.y);
    draw->AddRectFilled(origin, canvas_end, rgba(19, 30, 47), 5.f);
    draw->AddRect(origin, canvas_end, rgba(69, 91, 121), 5.f);
    draw->PushClipRect(origin, canvas_end, true);
    const auto point = [&](float x, float y) {
        return ImVec2(origin.x + graph_pan_x_ + x * graph_scale_,
                      origin.y + graph_pan_y_ + y * graph_scale_);
    };
    std::vector<ImVec2> positions(view.nodes.size());
    std::size_t tasks = 0, resources = 0, evidence = 0;
    for (std::size_t i = 0; i < view.nodes.size(); ++i) {
        const auto kind = view.nodes[i].kind;
        if (kind == GraphNodeKind::Task)
            positions[i] = point(30.f, 45.f + 57.f * static_cast<float>(tasks++));
        else if (kind == GraphNodeKind::Resource)
            positions[i] = point(310.f, 45.f + 57.f * static_cast<float>(resources++));
        else
            positions[i] = point(590.f, 45.f + 57.f * static_cast<float>(evidence++));
    }
    for (const auto &edge : view.edges) {
        if (edge.from >= positions.size() || edge.to >= positions.size())
            continue;
        if (!graph_show_evidence_ && edge.relation == "supports")
            continue;
        const auto &src = view.nodes[edge.from];
        const auto &dst = view.nodes[edge.to];
        if (!graph_show_evidence_ &&
            (src.kind == GraphNodeKind::Evidence || dst.kind == GraphNodeKind::Evidence))
            continue;
        const bool highlighted = src.id == graph_selected_ || dst.id == graph_selected_;
        const ImVec2 a = positions[edge.from], b = positions[edge.to];
        const float w = 178.f * graph_scale_, h = 36.f * graph_scale_;
        draw->AddLine(ImVec2(a.x + w / 2.f, a.y + h / 2.f), ImVec2(b.x + w / 2.f, b.y + h / 2.f),
                      highlighted                   ? rgba(255, 220, 135)
                      : edge.relation == "supports" ? rgba(109, 123, 156, 85)
                                                    : rgba(109, 194, 225, 155),
                      highlighted ? 2.5f : 1.0f);
    }
    for (std::size_t i = 0; i < view.nodes.size(); ++i) {
        const auto &node = view.nodes[i];
        if (!graph_show_evidence_ && node.kind == GraphNodeKind::Evidence)
            continue;
        const ImVec2 a = positions[i];
        const ImVec2 b(a.x + 178.f * graph_scale_, a.y + 36.f * graph_scale_);
        if (b.x < origin.x || a.x > canvas_end.x || b.y < origin.y || a.y > canvas_end.y)
            continue;
        const bool selected = node.id == graph_selected_;
        const ImU32 colour = node.kind == GraphNodeKind::Task       ? rgba(37, 109, 159)
                             : node.kind == GraphNodeKind::Resource ? rgba(157, 102, 46)
                                                                    : rgba(97, 74, 140);
        draw->AddRectFilled(a, b, colour, 5.f);
        draw->AddRect(a, b, selected ? rgba(255, 239, 158) : rgba(164, 185, 213), 5.f, 0,
                      selected ? 3.f : 1.f);
        std::string text = node.label;
        if (text.size() > 22)
            text = text.substr(0, 19) + "...";
        draw->AddText(ImVec2(a.x + 8.f, a.y + 9.f * graph_scale_), rgba(250, 252, 255),
                      text.c_str());
        const bool node_hovered = hovered && ImGui::IsMouseHoveringRect(a, b);
        if (node_hovered) {
            ImGui::SetTooltip("%s", node.id.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                graph_selected_ = node.id;
        }
    }
    draw->PopClipRect();
    if (graph_selected_.empty()) {
        ImGui::TextDisabled(
            "Select a task, resource or event node to inspect its supporting records.");
        return;
    }
    ImGui::SeparatorText("Selected evidence links");
    ImGui::TextWrapped("%s", graph_selected_.c_str());
    std::size_t selected_idx = view.nodes.size();
    for (std::size_t i = 0; i < view.nodes.size(); ++i)
        if (view.nodes[i].id == graph_selected_) {
            selected_idx = i;
            break;
        }
    if (selected_idx == view.nodes.size())
        return;
    std::size_t shown = 0;
    for (const auto &edge : view.edges) {
        if (edge.relation == "supports" || (edge.from != selected_idx && edge.to != selected_idx))
            continue;
        if (++shown > 60)
            break;
        ImGui::PushID(static_cast<int>(edge.event_index));
        const std::string item = edge.relation + " at " + as_text(edge.timestamp_us) +
                                 " us; line " + as_text(edge.source_line) + "; event #" +
                                 as_text(edge.event_index);
        if (ImGui::SmallButton("Inspect event")) {
            focused_event_ = edge.event_index;
            rebuild_event_filter();
            page_ = Page::Events;
        }
        ImGui::SameLine();
        label(item);
        ImGui::PopID();
    }
    if (shown > 60)
        ImGui::TextDisabled("Evidence list capped at 60 visible relationships.");
    if (const auto &node = view.nodes[selected_idx]; node.event_index) {
        if (ImGui::Button("Open selected evidence event")) {
            focused_event_ = *node.event_index;
            rebuild_event_filter();
            page_ = Page::Events;
        }
    }
}

void Workspace::draw_cpu() {
    if (!timeline_) {
        ImGui::TextDisabled("Select a valid observed time window in Execution timeline.");
        return;
    }
    header("CPU time by core");
    ImGui::TextDisabled("Each core is reported separately. Unobserved time remains unknown.");
    if (ImGui::BeginTable("cpu", 6, table_flags, ImVec2(0, 230))) {
        for (const char *col :
             {"Core", "Running (us)", "ISR (us)", "Idle (us)", "Unknown (us)", "Busy %"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (const auto &c : timeline_->cores) {
            ImGui::TableNextRow();
            cell(c.core);
            cell(as_text(c.running_us));
            cell(as_text(c.isr_us));
            cell(as_text(c.idle_us));
            cell(as_text(c.unknown_us));
            cell(c.busy_percent ? std::to_string(*c.busy_percent) + "%" : "Unknown");
        }
        ImGui::EndTable();
    }
    header("Task activity and completed waits");
    if (ImGui::BeginTable("waits", 4, table_flags, ImVec2(0, 260))) {
        for (const char *col : {"Task", "Running (us)", "Completed wait (us)", "Wait count"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        std::size_t count = 0;
        for (const auto &t : timeline_->tasks) {
            if (!t.running_us && !t.completed_waits)
                continue;
            if (++count > 2000)
                break;
            ImGui::TableNextRow();
            cell(t.task);
            cell(as_text(t.running_us));
            cell(as_text(t.completed_wait_us));
            cell(as_text(t.completed_waits));
        }
        ImGui::EndTable();
    }
    for (const auto &warning : timeline_->warnings)
        ImGui::TextColored(ImVec4(1.f, .75f, .42f, 1), "%s", warning.c_str());
    ImGui::TextDisabled("Busy includes ISR time; unknown scheduler time is never assumed idle.");
}
void Workspace::draw_replay() {
    if (!controller_.loaded()) {
        ImGui::TextDisabled("Open a capture to navigate its recorded event history.");
        return;
    }
    header("Time-travel replay");
    ImGui::TextDisabled(
        "Deterministic offline observations, not live or reconstructed RTOS memory state.");
    const auto &events = controller_.trace().capture.events;
    if (events.empty()) {
        ImGui::TextDisabled("Capture contains no events.");
        return;
    }
    const std::size_t count = std::min<std::size_t>(events.size(), 100000);
    const std::uint64_t first = 0;
    const std::uint64_t last = static_cast<std::uint64_t>(count - 1);
    bool changed = false;
    if (ImGui::Button("First event")) {
        replay_cursor_ = first;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Previous") && replay_cursor_ > first) {
        --replay_cursor_;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && replay_cursor_ < last) {
        ++replay_cursor_;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Last in budget")) {
        replay_cursor_ = last;
        changed = true;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderScalar("Event index", ImGuiDataType_U64, &replay_cursor_, &first, &last))
        changed = true;
    if (changed || !replay_)
        update_replay();
    if (!replay_)
        return;
    const auto &state = *replay_;
    const auto &event = events[static_cast<std::size_t>(replay_cursor_)];
    ImGui::Text("Event #%llu | %s | %llu us | Line %zu | Clock: %s",
                static_cast<unsigned long long>(replay_cursor_), event.type.c_str(),
                static_cast<unsigned long long>(event.timestamp_us), event.line,
                state.clock_domain.c_str());
    if (events.size() > count)
        ImGui::TextColored(ImVec4(1.f, .75f, .42f, 1.f),
                           "Preview capped at first %zu events; later history is not replayed.",
                           count);
    if (!state.integrity_verified)
        ImGui::TextColored(
            ImVec4(1.f, .75f, .42f, 1.f),
            "UNVERIFIED continuity: observations cannot establish exact historic state.");
    for (const auto &warning : state.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    if (ImGui::Button("Inspect this event")) {
        focused_event_ = static_cast<std::size_t>(replay_cursor_);
        rebuild_event_filter();
        page_ = Page::Events;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open causal graph at this event")) {
        graph_event_boundary_ = static_cast<std::size_t>(replay_cursor_);
        begin_us_ = controller_.report().start_us;
        end_us_ = event.timestamp_us;
        update_timeline();
        page_ = Page::Graph;
    }
    header("Last recorded mutex/resource transitions");
    ImGui::TextDisabled("Acquired/released means last observed transition; continuity is not "
                        "independently proved.");
    if (ImGui::BeginTable("replay_resources", 5, table_flags, ImVec2(0, 245))) {
        for (const char *col :
             {"Resource", "Observed transition", "Last task", "Event", "Time (us)"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (const auto &item : state.resources) {
            ImGui::TableNextRow();
            cell(item.id);
            cell(item.kind == ReplayResourceKind::Acquired   ? "Acquire recorded"
                 : item.kind == ReplayResourceKind::Released ? "Release recorded"
                                                             : "Unknown");
            cell(item.last_owner.empty() ? "Unknown" : item.last_owner);
            cell(as_text(item.event_index));
            cell(as_text(item.timestamp_us));
        }
        ImGui::EndTable();
    }
    header("Last recorded task waits");
    if (ImGui::BeginTable("replay_waits", 4, table_flags, ImVec2(0, 190))) {
        for (const char *col : {"Task", "Resource", "Last transition", "Event"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (const auto &item : state.waits) {
            ImGui::TableNextRow();
            cell(item.task);
            cell(item.resource);
            cell(!item.known ? "Unknown" : item.active ? "Wait began" : "Wait ended");
            cell(as_text(item.event_index));
        }
        ImGui::EndTable();
    }
}

void Workspace::draw_diagnostics() {
    if (!controller_.loaded() || !diagnostics_) {
        ImGui::TextDisabled("Open a normalized trace to see observed timing diagnostics.");
        return;
    }
    header("Scheduling diagnostics");
    ImGui::TextDisabled(
        "Observed release jitter, drift, response and execution interruption patterns.");
    ImGui::TextDisabled(
        "Observed maximum is not theoretical WCET; TASK_BEGIN is not scheduler switch-in.");
    ImGui::TextDisabled("Deadlines must be explicitly supplied: no period-as-deadline assumption.");
    const auto &d = *diagnostics_;
    if (!d.evidence_verified)
        ImGui::TextColored(ImVec4(1.f, .75f, .42f, 1.f),
                           "Unverified/incomplete evidence: exact scheduler history is unknown.");
    for (const auto &warning : d.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    header("Optional relative deadline");
    ImGui::SetNextItemWidth(175);
    ImGui::InputText("Task ID", deadline_task_.data(), deadline_task_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::InputScalar("Deadline (us)", ImGuiDataType_U64, &deadline_value_us_);
    ImGui::SameLine();
    if (ImGui::Button("Apply deadline")) {
        const std::string task = deadline_task_.data();
        const auto exists =
            std::any_of(controller_.report().threads.begin(), controller_.report().threads.end(),
                        [&](const Thread &item) { return item.id == task; });
        if (!exists || deadline_value_us_ == 0 ||
            deadline_value_us_ >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            error_ = "Choose an observed task and a positive 64-bit relative deadline.";
        } else {
            relative_deadlines_[task] = deadline_value_us_;
            error_.clear();
            update_diagnostics();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear deadline")) {
        relative_deadlines_.erase(deadline_task_.data());
        update_diagnostics();
    }
    if (ImGui::BeginTable("diagnostic-summary", 8, table_flags, ImVec2(0, 230))) {
        for (const char *col : {"Task", "Releases", "Response max", "Begin delay max",
                                "Abs jitter max", "Last phase", "Deadline misses", "Interruptions"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (const auto &task : d.tasks) {
            ImGui::TableNextRow();
            cell(task.task);
            cell(as_text(task.releases));
            cell(task.max_response_us ? as_text(*task.max_response_us) + " us" : "Unknown");
            cell(task.max_dispatch_delay_us ? as_text(*task.max_dispatch_delay_us) + " us"
                                            : "Unknown");
            cell(task.max_abs_release_jitter_us ? as_text(*task.max_abs_release_jitter_us) + " us"
                                                : "Unknown");
            cell(task.last_phase_drift_us ? std::to_string(*task.last_phase_drift_us) + " us"
                                          : "Unknown");
            cell(task.explicit_deadline_us ? as_text(task.observed_deadline_misses)
                                           : "No deadline");
            cell(as_text(task.observed_interruptions));
        }
        ImGui::EndTable();
    }
    header("Measured samples and linked evidence");
    ImGui::TextDisabled(
        "Each record links its last supporting source event. No inferred missing events.");
    if (ImGui::BeginTable("diagnostic-samples", 6, table_flags, ImVec2(0, -1))) {
        for (const char *col :
             {"Metric", "Task", "Start (us)", "End (us)", "Value (us)", "Evidence"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(d.samples.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto &sample = d.samples[static_cast<std::size_t>(row)];
                ImGui::PushID(row);
                ImGui::TableNextRow();
                cell(sample.code);
                cell(sample.task);
                cell(as_text(sample.from_us));
                cell(as_text(sample.to_us));
                cell(std::to_string(sample.measured_us));
                ImGui::TableNextColumn();
                if (sample.last_event &&
                    *sample.last_event < controller_.trace().capture.events.size()) {
                    if (ImGui::SmallButton("Inspect source")) {
                        focused_event_ = *sample.last_event;
                        rebuild_event_filter();
                        page_ = Page::Events;
                    }
                } else
                    ImGui::TextDisabled("Unlinked");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%s", sample.detail.c_str(),
                                      sample.evidence_verified
                                          ? "Evidence continuity verified"
                                          : "Incomplete/unverified provenance");
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
}

void Workspace::draw_resources() {
    if (!controller_.loaded() || !resources_) {
        ImGui::TextDisabled(
            "Open a normalized offline trace to inspect recorded resource operations.");
        return;
    }
    header("Resource and wait observations");
    const auto &view = *resources_;
    ImGui::TextDisabled(
        "Task/resource events only. Queue depth, semaphore count and wakeup cause are unknown.");
    ImGui::TextDisabled(
        "Mutex owner is the LAST RECORDED acquire, not guaranteed current kernel ownership.");
    if (!view.evidence_verified || view.limited)
        ImGui::TextColored(
            ImVec4(1.f, .76f, .42f, 1.f),
            "Unverified/capped evidence: missing resource transitions must remain unknown.");
    ImGui::Text("Examined %zu events | %zu resources | RTOS label: %s | Clock: %s",
                view.examined_events, view.resources.size(), view.rtos_label.c_str(),
                view.clock_domain.c_str());
    for (const auto &warning : view.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    ImGui::SetNextItemWidth(270.f);
    ImGui::InputText("Find resource", resource_filter_.data(), resource_filter_.size());
    if (ImGui::BeginTable("resources-list", 7, table_flags, ImVec2(0, 255.f))) {
        for (const char *col : {"Resource", "Type", "Wait began", "Completed waits",
                                "Unresolved waits", "Max completed (us)", "Last mutex owner"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < view.resources.size(); ++i) {
            const auto &r = view.resources[i];
            if (resource_filter_[0] != '\0' &&
                r.name.find(resource_filter_.data()) == std::string::npos)
                continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(r.name.c_str(), r.name == resource_selected_,
                                  ImGuiSelectableFlags_SpanAllColumns))
                resource_selected_ = r.name;
            cell(resource_kind_name(r.kind));
            cell(as_text(r.observed_wait_starts));
            cell(as_text(r.completed_waits));
            cell(as_text(r.unresolved_waits));
            cell(r.max_completed_wait_us ? as_text(*r.max_completed_wait_us) : "Unknown");
            cell(r.owner_observation_valid && r.last_recorded_owner
                     ? *r.last_recorded_owner + " (observed)"
                     : "Unknown / released");
            if (ImGui::IsItemHovered() && r.continuity_lost)
                ImGui::SetTooltip("Ownership/wait continuity was lost; no inferred state.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (resource_selected_.empty()) {
        ImGui::TextDisabled("Select a resource to inspect source events and anomalies.");
        return;
    }
    header("Evidence for selected resource");
    ImGui::Text("Selected: %s", resource_selected_.c_str());
    ImGui::TextDisabled("Only matched starts/ends yield completed wait durations.");
    if (ImGui::BeginTable("resource-evidence", 5, table_flags, ImVec2(0, 235.f))) {
        for (const char *name : {"Time (us)", "Event", "Task", "Completed wait (us)", "Source"})
            ImGui::TableSetupColumn(name);
        ImGui::TableHeadersRow();
        std::size_t shown = 0;
        for (const auto &observation : view.observations) {
            if (observation.resource != resource_selected_)
                continue;
            if (shown++ >= 160)
                break; // bounded on-screen detail
            ImGui::PushID(static_cast<int>(observation.event_index));
            ImGui::TableNextRow();
            cell(as_text(observation.timestamp_us));
            cell(observation.type);
            cell(observation.task);
            cell(observation.completed_wait_us ? as_text(*observation.completed_wait_us)
                                               : "Not a completed wait");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Inspect event")) {
                focused_event_ = observation.event_index;
                rebuild_event_filter();
                page_ = Page::Events;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Source event #%zu, line %zu, clock %s%s",
                                  observation.event_index, observation.source_line,
                                  view.clock_domain.c_str(),
                                  observation.evidence_verified ? "" : "\nContinuity unverified");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    header("Observed anomalies (not proven root causes)");
    std::size_t anomaly_rows = 0;
    for (const auto &item : view.anomalies) {
        if (item.resource != resource_selected_ && !item.resource.empty() && item.resource != "-")
            continue;
        if (anomaly_rows++ >= 50)
            break;
        ImGui::PushID(static_cast<int>(item.event_index));
        if (ImGui::SmallButton("Show source")) {
            focused_event_ = item.event_index;
            rebuild_event_filter();
            page_ = Page::Events;
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s [%s / %s]: %s", item.code.c_str(), item.task.c_str(),
                           item.resource.c_str(), item.explanation.c_str());
        ImGui::PopID();
    }
    if (anomaly_rows == 0)
        ImGui::TextDisabled("No recorded inconsistency for this resource in the scanned window.");
}

void Workspace::draw_events() {
    if (!controller_.loaded())
        return;
    header("Recorded events");
    if (ImGui::InputText("Filter type, task, object or time", filter_.data(), filter_.size())) {
        focused_event_.reset();
        rebuild_event_filter();
    }
    if (focused_event_ && *focused_event_ < controller_.trace().capture.events.size()) {
        const auto &e = controller_.trace().capture.events[*focused_event_];
        ImGui::Text("Graph evidence #%zu: %s at %llu us (source line %zu)", *focused_event_,
                    e.type.c_str(), static_cast<unsigned long long>(e.timestamp_us), e.line);
        ImGui::SameLine();
        if (ImGui::Button("Show all recorded events")) {
            focused_event_.reset();
            rebuild_event_filter();
        }
    }
    ImGui::TextDisabled("Displaying %zu rows; preview capped at first 20,000 events.",
                        filtered_.size());
    if (ImGui::BeginTable("events", 6, table_flags, ImVec2(0, -1))) {
        for (const char *col : {"Time (us)", "Type", "Task", "Object / core", "Value", "Line"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        const auto &events = controller_.trace().capture.events;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &e = events[filtered_[static_cast<std::size_t>(i)]];
                ImGui::TableNextRow();
                cell(as_text(e.timestamp_us));
                cell(e.type);
                cell(e.task);
                cell(e.object);
                cell(std::to_string(e.value));
                cell(std::to_string(e.line));
            }
        ImGui::EndTable();
    }
}
void Workspace::draw_causality() {
    if (!controller_.loaded() || !causality_) {
        ImGui::TextDisabled(
            "Open a normalized offline trace to inspect recorded mutex relationships.");
        return;
    }
    header("Causal diagnostics");
    ImGui::TextDisabled(
        "Observed patterns only: no proven scheduling cause, starvation or kernel deadlock.");
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::InputScalar("Long mutex wait threshold (us; 0 = off)", ImGuiDataType_U64,
                           &long_mutex_wait_threshold_us_))
        update_causality();
    if (!causality_)
        return;
    const auto &view = *causality_;
    if (!view.evidence_complete || view.limited)
        ImGui::TextColored(
            ImVec4(1.f, .76f, .42f, 1.f),
            "Incomplete/limited evidence: confidence downgraded; unobserved state unknown.");
    ImGui::Text("Scanned %zu events | %zu patterns | Clock: %s", view.scanned_events,
                view.findings.size(), view.clock_domain.c_str());
    for (const auto &warn : view.warnings)
        ImGui::TextWrapped("%s", warn.c_str());
    if (view.findings.empty()) {
        ImGui::TextDisabled("No supported causal pattern found in the bounded evidence.");
        return;
    }
    if (ImGui::BeginTable("causal-patterns", 6, table_flags, ImVec2(0, 270.f))) {
        for (const char *column :
             {"Pattern", "Confidence", "Task", "Resource", "Related task", "Measured (us)"})
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < view.findings.size(); ++i) {
            const auto &f = view.findings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(f.code.c_str(), selected_causal_finding_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                selected_causal_finding_ = i;
            cell(f.confidence);
            cell(f.task);
            cell(f.resource);
            cell(f.related_task.empty() ? "Unknown" : f.related_task);
            cell(as_text(f.observed_us));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const auto &f = view.findings[std::min(selected_causal_finding_, view.findings.size() - 1)];
    header("Why this candidate was reported");
    ImGui::TextWrapped("%s", f.explanation.c_str());
    ImGui::Text("Observed time window: [%llu, %llu] us",
                static_cast<unsigned long long>(f.start_us),
                static_cast<unsigned long long>(f.end_us));
    ImGui::TextDisabled(
        "Source order is retained. Click any event to inspect the original decoded row.");
    if (ImGui::BeginTable("causal-proof", 5, table_flags, ImVec2(0, 0))) {
        for (const char *column :
             {"Event #", "Timestamp (us)", "Type", "Task / resource", "Source"})
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        const auto &events = controller_.trace().capture.events;
        for (const std::size_t i : f.evidence_events) {
            if (i >= events.size())
                continue;
            const auto &e = events[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            cell(as_text(i));
            cell(as_text(e.timestamp_us));
            cell(e.type);
            cell(e.task + " / " + e.object);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Inspect event")) {
                focused_event_ = i;
                rebuild_event_filter();
                page_ = Page::Events;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Source line %zu / Event %zu", e.line, i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void Workspace::draw_faults() {
    header("Fault and memory evidence");
    ImGui::TextDisabled(
        "Local, read-only, self-reported snapshots. No target/probe reads or guessed TCB layout.");
    ImGui::SetNextItemWidth(520.f);
    ImGui::InputText("Snapshot file", fault_path_.data(), fault_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Open fault snapshot"))
        (void)open_fault_snapshot(fault_path_.data());
    if (!fault_snapshot_ || !fault_view_) {
        ImGui::TextDisabled("Open a versioned W12 fault snapshot to inspect TCB, stack, registers "
                            "and saved bytes.");
        ImGui::TextDisabled(
            "Try examples/fault-snapshot.tsv (synthetic, not a physical board dump).");
        return;
    }
    const auto &s = *fault_snapshot_;
    const auto &view = *fault_view_;
    ImGui::Text("Architecture label: %s / %u-bit | Firmware label: %s", s.architecture.c_str(),
                s.address_bits, s.firmware_label.c_str());
    ImGui::Text("Capture: %s | Source records: %zu", s.capture_id.c_str(), view.examined_records);
    ImGui::TextColored(
        ImVec4(1.f, .76f, .42f, 1.f),
        "Not attested. Source claims coherence: %s; acquisition was not independently checked.",
        view.claimed_coherent ? "yes" : "unverified");
    for (const auto &msg : view.warnings)
        ImGui::TextWrapped("%s", msg.c_str());
    if (ImGui::CollapsingHeader("Memory map", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("fault_regions", 5, table_flags, ImVec2(0, 145.f))) {
            for (const char *h : {"Region", "Kind", "Base (hex)", "Bytes (hex)", "Access"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const auto &r : s.regions) {
                ImGui::TableNextRow();
                cell(r.name);
                cell(r.kind);
                char addr[32], bytes[32];
                std::snprintf(addr, sizeof(addr), "0x%llx",
                              static_cast<unsigned long long>(r.address));
                std::snprintf(bytes, sizeof(bytes), "0x%llx",
                              static_cast<unsigned long long>(r.size));
                cell(addr);
                cell(bytes);
                cell(r.access);
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Explicit task/TCB/stack metadata",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Stack bytes used are source-reported only, never computed by scanning memory.");
        if (ImGui::BeginTable("fault_threads", 6, table_flags, ImVec2(0, 145.f))) {
            for (const char *h :
                 {"Task", "TCB address", "TCB bytes", "Stack base", "Stack bytes", "Recorded SP"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const auto &t : s.threads) {
                ImGui::TableNextRow();
                cell(t.name);
                char b[32];
                std::snprintf(b, sizeof(b), "0x%llx",
                              static_cast<unsigned long long>(t.tcb_address));
                cell(b);
                cell(as_text(t.tcb_size));
                std::snprintf(b, sizeof(b), "0x%llx",
                              static_cast<unsigned long long>(t.stack_address));
                cell(b);
                cell(as_text(t.stack_size));
                if (t.observed_sp) {
                    std::snprintf(b, sizeof(b), "0x%llx",
                                  static_cast<unsigned long long>(*t.observed_sp));
                    cell(b);
                } else
                    cell("Unknown (not read)");
                if (ImGui::IsItemHovered() && t.reported_stack_used_bytes)
                    ImGui::SetTooltip(
                        "Reported stack used: %llu bytes (unverified)",
                        static_cast<unsigned long long>(*t.reported_stack_used_bytes));
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Source-reported heap metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Heap used and peak are source-provided statistics, not a "
                            "reconstructed allocation graph.");
        if (ImGui::BeginTable("fault_heaps", 5, table_flags, ImVec2(0, 100.f))) {
            for (const char *h :
                 {"Heap", "Base (hex)", "Capacity (bytes)", "Used (bytes)", "Peak (bytes)"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const auto &h : s.heaps) {
                ImGui::TableNextRow();
                cell(h.name);
                char addr[32];
                std::snprintf(addr, sizeof(addr), "0x%llx",
                              static_cast<unsigned long long>(h.address));
                cell(addr);
                cell(as_text(h.size));
                cell(h.reported_used_bytes ? as_text(*h.reported_used_bytes) : "Unknown");
                cell(h.reported_peak_bytes ? as_text(*h.reported_peak_bytes) : "Unknown");
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Saved registers and fault markers",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("fault_regs", 4, table_flags, ImVec2(0, 145.f))) {
            for (const char *h : {"Context", "Register", "Value", "Source line"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const auto &r : s.registers) {
                ImGui::TableNextRow();
                cell(r.context);
                cell(r.name);
                if (r.value) {
                    char b[32];
                    std::snprintf(b, sizeof(b), "0x%llx",
                                  static_cast<unsigned long long>(*r.value));
                    cell(b);
                } else
                    cell("READ FAILED");
                cell(as_text(r.line));
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Saved memory read samples")) {
        ImGui::TextDisabled(
            "Preview is limited to 16 bytes per record; failed reads have no assumed bytes.");
        if (ImGui::BeginTable("fault_mem", 5, table_flags, ImVec2(0, 135.f))) {
            for (const char *h : {"Region", "Address", "Read", "Bytes", "Hex preview"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            static constexpr char digits[] = "0123456789abcdef";
            for (const auto &m : s.memory) {
                ImGui::TableNextRow();
                cell(m.region);
                char addr[32];
                std::snprintf(addr, sizeof(addr), "0x%llx",
                              static_cast<unsigned long long>(m.address));
                cell(addr);
                cell(m.read_succeeded ? "Recorded" : "FAILED");
                cell(as_text(m.bytes.size()));
                std::string preview;
                for (std::size_t i = 0; i < std::min<std::size_t>(16, m.bytes.size()); ++i) {
                    if (i)
                        preview += ' ';
                    preview += digits[m.bytes[i] >> 4];
                    preview += digits[m.bytes[i] & 15];
                }
                cell(preview.empty() ? "Unknown" : preview);
            }
            ImGui::EndTable();
        }
    }
    header("Evidence-linked checks (candidate findings only)");
    ImGui::TextDisabled("Each source is a physical line in the opened local snapshot file.");
    if (view.findings.empty()) {
        ImGui::TextDisabled(
            "No structural inconsistencies recorded. This is not a clean-bill-of-health verdict.");
        return;
    }
    if (ImGui::BeginTable("fault_findings", 3, table_flags, ImVec2(0, 220.f))) {
        for (const char *h : {"Finding", "Subject", "Source line(s)"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < view.findings.size(); ++i) {
            const auto &f = view.findings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(f.code.c_str(), fault_selected_finding_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                fault_selected_finding_ = i;
            cell(f.subject);
            std::string lines;
            for (const auto l : f.source_lines) {
                if (!lines.empty())
                    lines += ", ";
                lines += std::to_string(l);
            }
            cell(lines);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const auto &f = view.findings[std::min(fault_selected_finding_, view.findings.size() - 1)];
    ImGui::SeparatorText("Selected evidence");
    ImGui::TextWrapped("%s", f.explanation.c_str());
    for (std::size_t i = 0; i < f.source_lines.size(); ++i) {
        const auto line = f.source_lines[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("Snapshot source line %zu", line);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy source reference")) {
            const auto reference = fault_origin_ + ":" + std::to_string(line);
            ImGui::SetClipboardText(reference.c_str());
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled(
        "Decoded Cortex-M status bits require confirmed architecture + target metadata.");
}

void Workspace::draw_source_graph() {
    header("C / C++ / Rust source and symbol graph");
    ImGui::TextDisabled(
        "Offline, read-only. ELF and source SHA-256 matching is required for source navigation.");
    ImGui::TextDisabled("Function/call/lock relationships are declared by a sidecar, NOT extracted "
                        "or validated from DWARF.");
    ImGui::SetNextItemWidth(510.f);
    ImGui::InputText("Sidecar TSV", symbol_manifest_path_.data(), symbol_manifest_path_.size());
    ImGui::SetNextItemWidth(510.f);
    ImGui::InputText("Local ELF", symbol_elf_path_.data(), symbol_elf_path_.size());
    ImGui::SetNextItemWidth(510.f);
    ImGui::InputText("Source root", symbol_source_root_.data(), symbol_source_root_.size());
    if (ImGui::Button("Open and verify source index")) {
        (void)open_source_index(symbol_manifest_path_.data(), symbol_elf_path_.data(),
                                symbol_source_root_.data());
    }
    if (!symbols_) {
        ImGui::TextDisabled("Load a v1 source sidecar, local ELF and matching source directory.");
        return;
    }
    const auto &index = *symbols_;
    const auto &verification = symbol_verification_;
    ImGui::Text("Firmware label: %s  |  %u-bit | functions: %zu  | calls: %zu  | locks: %zu",
                index.firmware_label.c_str(), index.address_bits, index.functions.size(),
                index.calls.size(), index.locks.size());
    ImGui::TextColored(
        verification.elf_matches ? ImVec4(.46f, .85f, .69f, 1.f) : ImVec4(1.f, .73f, .41f, 1.f),
        "ELF content SHA-256: %s (byte identity only, not proof of valid symbol records)",
        verification.elf_matches ? "MATCHED" : "UNKNOWN / MISMATCH");
    ImGui::Text("Source checksums: %zu of %zu matched", verification.verified_sources.size(),
                index.source_sha256.size());
    for (const auto &warning : verification.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    if (index.functions.empty()) {
        ImGui::TextDisabled("No source function records were supplied.");
        return;
    }
    selected_symbol_ = std::min(selected_symbol_, index.functions.size() - 1);
    header("Address to symbol (exact declared range only)");
    ImGui::SetNextItemWidth(205.f);
    ImGui::InputScalar("PC/address (decimal)", ImGuiDataType_U64, &address_lookup_);
    ImGui::SameLine();
    if (ImGui::Button("Resolve address")) {
        const auto resolved = resolve_source_pc(index, verification, address_lookup_);
        if (resolved.function_index) {
            selected_symbol_ = *resolved.function_index;
            source_excerpt_.reset();
            error_.clear();
        } else
            error_ = resolved.ambiguous
                         ? "Overlapping function ranges: address resolution ambiguous"
                         : "No unique symbol for PC (or ELF identity unverified)";
    }
    if (fault_snapshot_) {
        const auto label_match = fault_snapshot_->firmware_label == index.firmware_label &&
                                 fault_snapshot_->address_bits == index.address_bits;
        if (!label_match)
            ImGui::TextColored(
                ImVec4(1.f, .73f, .41f, 1.f),
                "W12 snapshot firmware/width labels differ: automatic PC association withheld.");
        else if (verification.elf_matches) {
            for (const auto &reg : fault_snapshot_->registers) {
                if (reg.name != "PC" || !reg.value)
                    continue;
                const auto matched = resolve_source_pc(index, verification, *reg.value);
                ImGui::PushID(static_cast<int>(reg.line));
                if (matched.function_index) {
                    const auto &f = index.functions[*matched.function_index];
                    const std::string message =
                        reg.context + " PC -> " + f.name + " (source-sidecar candidate)";
                    if (ImGui::SmallButton("Inspect saved PC")) {
                        selected_symbol_ = *matched.function_index;
                        address_lookup_ = *reg.value;
                        source_excerpt_.reset();
                    }
                    ImGui::SameLine();
                    label(message);
                } else if (matched.ambiguous) {
                    ImGui::TextDisabled("%s PC: overlapping function ranges", reg.context.c_str());
                } else
                    ImGui::TextDisabled("%s PC: no declared matching function",
                                        reg.context.c_str());
                ImGui::PopID();
            }
            ImGui::TextDisabled("Firmware labels are self-reported; W12 snapshot and ELF are not "
                                "cryptographically paired.");
        }
    }
    header("Declared functions");
    if (ImGui::BeginTable("source_function_list", 5, table_flags, ImVec2(0, 175.f))) {
        for (const char *col : {"Function", "Language", "PC start", "Bytes", "Declared file:line"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(index.functions.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t i = static_cast<std::size_t>(row);
                const auto &f = index.functions[i];
                ImGui::PushID(row);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(f.name.c_str(), selected_symbol_ == i,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_symbol_ = i;
                    source_excerpt_.reset();
                }
                cell(f.language);
                char pc[32];
                std::snprintf(pc, sizeof(pc), "0x%llx", static_cast<unsigned long long>(f.address));
                cell(pc);
                cell(as_text(f.size));
                cell(f.path + ":" + std::to_string(f.line));
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    const auto &selected = index.functions[selected_symbol_];
    header("Source relationship graph");
    const auto target_name = [&](const std::string &id) -> std::string {
        const auto it = std::find_if(index.functions.begin(), index.functions.end(),
                                     [&](const SourceFunction &f) { return f.id == id; });
        return it == index.functions.end() ? "Unknown" : it->name;
    };
    std::vector<std::string> inbound, outbound, locks;
    std::size_t extra_in = 0, extra_out = 0, extra_lock = 0;
    for (const auto &call : index.calls) {
        if (call.callee == selected.id) {
            if (inbound.size() < 4)
                inbound.push_back(target_name(call.caller));
            else
                ++extra_in;
        }
        if (call.caller == selected.id) {
            if (outbound.size() < 4)
                outbound.push_back(target_name(call.callee));
            else
                ++extra_out;
        }
    }
    for (const auto &lock : index.locks)
        if (lock.function == selected.id) {
            if (locks.size() < 4)
                locks.push_back(lock.operation + ": " + lock.resource);
            else
                ++extra_lock;
        }
    if (ImGui::BeginChild("source_graph_canvas", ImVec2(0, 230), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const auto line = IM_COL32(113, 164, 210, 245);
        const auto rectangle = [&](float x, float y, const std::string &text, bool focus = false) {
            const ImVec2 p(o.x + x, o.y + y);
            draw->AddRectFilled(p, ImVec2(p.x + 170.f, p.y + 32.f),
                                focus ? IM_COL32(35, 106, 149, 255) : IM_COL32(43, 57, 75, 255),
                                6.f);
            draw->AddRect(p, ImVec2(p.x + 170.f, p.y + 32.f), line, 6.f);
            const std::string clipped = text.size() > 23 ? text.substr(0, 20) + "..." : text;
            draw->AddText(ImVec2(p.x + 7.f, p.y + 9.f), IM_COL32(244, 249, 255, 255),
                          clipped.c_str());
        };
        rectangle(330.f, 72.f, selected.name, true);
        for (std::size_t i = 0; i < inbound.size(); ++i) {
            const float y = 8.f + 43.f * static_cast<float>(i);
            rectangle(35.f, y, inbound[i]);
            draw->AddLine(ImVec2(o.x + 205.f, o.y + y + 16.f), ImVec2(o.x + 330.f, o.y + 88.f),
                          line, 2.f);
        }
        for (std::size_t i = 0; i < outbound.size(); ++i) {
            const float y = 8.f + 43.f * static_cast<float>(i);
            rectangle(625.f, y, outbound[i]);
            draw->AddLine(ImVec2(o.x + 500.f, o.y + 88.f), ImVec2(o.x + 625.f, o.y + y + 16.f),
                          line, 2.f);
        }
        for (std::size_t i = 0; i < locks.size(); ++i) {
            const float x = 200.f + 170.f * static_cast<float>(i);
            rectangle(x, 173.f, locks[i]);
            draw->AddLine(ImVec2(o.x + 415.f, o.y + 104.f), ImVec2(o.x + x + 85.f, o.y + 173.f),
                          IM_COL32(232, 183, 88, 255), 2.f);
        }
        ImGui::Dummy(ImVec2(920.f, 218.f));
    }
    ImGui::EndChild();
    if (extra_in || extra_out || extra_lock)
        ImGui::TextDisabled("Visual preview omits %zu incoming, %zu outgoing and %zu lock links; "
                            "see sidecar evidence.",
                            extra_in, extra_out, extra_lock);
    ImGui::TextDisabled(
        "Graph edges are sidecar-declared only; no dynamic call stacks or actual locking proven.");
    header("Declared relationships and evidence lines");
    for (const auto &call : index.calls) {
        if (call.caller != selected.id && call.callee != selected.id)
            continue;
        ImGui::TextWrapped("CALL  %s  ->  %s  (declared source line %llu; manifest line %zu)",
                           target_name(call.caller).c_str(), target_name(call.callee).c_str(),
                           static_cast<unsigned long long>(call.line), call.manifest_line);
    }
    for (const auto &lock : index.locks)
        if (lock.function == selected.id)
            ImGui::TextWrapped("LOCK  %s  %s  (declared source line %llu; manifest line %zu)",
                               lock.operation.c_str(), lock.resource.c_str(),
                               static_cast<unsigned long long>(lock.line), lock.manifest_line);
    header("Verified local source preview");
    const auto verified_file = verification.verified_sources.contains(selected.path);
    if (!verification.elf_matches || !verified_file)
        ImGui::TextColored(
            ImVec4(1.f, .73f, .41f, 1.f),
            "Source jump blocked: ELF and selected source must both pass SHA-256 comparison.");
    ImGui::BeginDisabled(!verification.elf_matches || !verified_file);
    if (ImGui::Button("Jump to declared function line (local read-only)")) {
        try {
            source_excerpt_ =
                read_verified_source_excerpt(index, verification, selected_symbol_, 7);
            error_.clear();
        } catch (const std::exception &e) {
            source_excerpt_.reset();
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    if (source_excerpt_) {
        ImGui::TextDisabled("%s:%llu  (source hash rechecked on click)",
                            source_excerpt_->path.c_str(),
                            static_cast<unsigned long long>(source_excerpt_->target_line));
        if (ImGui::BeginChild("source_preview", ImVec2(0, 170), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto &[number, line] : source_excerpt_->lines) {
                const std::string row = std::to_string(number) + "  " + line;
                if (number == source_excerpt_->target_line)
                    ImGui::TextColored(ImVec4(.53f, .89f, .68f, 1.f), "%s", row.c_str());
                else
                    ImGui::TextUnformatted(row.c_str());
            }
        }
        ImGui::EndChild();
    }
}

void Workspace::draw_debug_bridge() {
    header("Eclipse DSF debugger lifecycle (passive)");
    ImGui::TextDisabled(
        "No debugger connection. No live GDB/MI commands. No target memory/register/flash access.");
    ImGui::TextDisabled(
        "This panel reads a local Eclipse-reported metadata snapshot ONLY when you click Refresh.");
    ImGui::SetNextItemWidth(540.f);
    ImGui::InputText("Status file", bridge_file_path_.data(), bridge_file_path_.size());
    if (ImGui::Button("Refresh local status"))
        (void)open_bridge_status(bridge_file_path_.data());
    ImGui::SameLine();
    if (ImGui::Button("Forget previous session / epoch")) {
        bridge_monitor_.clear();
        bridge_observation_.reset();
        notice_ = "Previous session forgotten locally. No debugger command was sent.";
    }
    if (!bridge_observation_) {
        ImGui::TextDisabled(
            "No passive session record imported. Select the active Eclipse DSF launch,");
        ImGui::TextDisabled("then enable the opt-in W14 handler in the owning Eclipse plugin.");
        return;
    }
    const auto &status = *bridge_observation_;
    if (!status.accepted)
        ImGui::TextColored(ImVec4(1.f, .75f, .43f, 1.f), "REJECTED lifecycle publication");
    const bool recent = status.accepted && status.fresh;
    const ImVec4 colour = recent ? ImVec4(.52f, .85f, .66f, 1.f) : ImVec4(1.f, .75f, .43f, 1.f);
    ImGui::TextColored(colour, "Publisher: %s | Recorded state: %s", status.status.c_str(),
                       bridge_state_name(status.record.state));
    ImGui::Text("Session: %s", status.record.session_nonce.c_str());
    ImGui::Text("Launch label: %s", status.record.launch_label.c_str());
    ImGui::Text("DSF session label: %s", status.record.dsf_session_label.c_str());
    ImGui::Text("Epoch: %llu | Publication sequence: %llu",
                static_cast<unsigned long long>(status.record.epoch),
                static_cast<unsigned long long>(status.record.sequence));
    ImGui::Text("Reporter timestamp: %llu ms since Unix epoch",
                static_cast<unsigned long long>(status.record.writer_unix_ms));
    ImGui::TextDisabled("Last refresh: %s | Transition continuity: %s",
                        status.fresh ? "recent at refresh" : "unknown / stale",
                        status.transition_known ? "observed" : "incomplete/unchanged");
    ImGui::TextWrapped("A recent metadata heartbeat does NOT guarantee a running target, debugger "
                       "health or coherent snapshot.");
    for (const auto &warning : status.warnings)
        ImGui::TextWrapped("%s", warning.c_str());
    header("Access policy");
    ImGui::BulletText("Debugger owner: Eclipse CDT / DSF-GDB (not Observatory)");
    ImGui::BulletText("Bridge capability: %s", status.record.metadata_only
                                                   ? "metadata-only, no commands"
                                                   : "UNKNOWN (rejected)");
    ImGui::BulletText("GDB/OpenOCD connections created by Observatory: 0");
    ImGui::BulletText("Memory/register acquisition: W15, NOT available in this panel");
    ImGui::TextDisabled(
        "The source Eclipse extension is not installed/qualified by this repository.");
}

void Workspace::draw_snapshot_receipt() {
    header("Approved snapshot receipt — read only");
    ImGui::TextColored(ImVec4(1.f, .74f, .43f, 1.f),
                       "UNVERIFIED: imported local file. No GDB target reads or acquisition "
                       "approval from this panel.");
    ImGui::TextDisabled(
        "Only the owning Eclipse DSF integration may request approved RAM/CPU-register reads.");
    ImGui::SetNextItemWidth(550.f);
    ImGui::InputText("Local W15 receipt", snapshot_receipt_path_.data(),
                     snapshot_receipt_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Open receipt"))
        (void)open_snapshot_receipt(snapshot_receipt_path_.data());
    if (!snapshot_receipt_) {
        ImGui::TextDisabled(
            "Try examples/approved-snapshot.tsv (synthetic, not hardware evidence).");
        return;
    }
    const auto &capture = *snapshot_receipt_;
    ImGui::Text("Reporter: %s | Session: %s | Epoch: %llu", capture.source_label.c_str(),
                capture.request.session_nonce.c_str(),
                static_cast<unsigned long long>(capture.owner_epoch));
    ImGui::Text("Approval (self-reported): %s | Address width: %u",
                capture.request.user_approval_id.c_str(), capture.request.address_bits);
    ImGui::TextDisabled("A receipt does NOT prove a stopped target, safe MMIO, source authenticity "
                        "or multi-core coherence.");
    std::size_t ok = 0, fail = 0, bytes = 0;
    for (const auto &read : capture.samples) {
        if (read.success) {
            ++ok;
            bytes += read.data.size();
        } else
            ++fail;
    }
    ImGui::Text("Recorded results: %zu success, %zu failed/unknown | %zu saved RAM bytes", ok, fail,
                bytes);
    if (ImGui::BeginTable("approved_snapshot_items", 6, table_flags, ImVec2(0, 330.f))) {
        for (const char *name :
             {"Kind", "Label", "Address", "Requested bytes", "Result", "Recorded value"})
            ImGui::TableSetupColumn(name);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < capture.samples.size(); ++i) {
            const auto &r = capture.samples[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            cell(r.requested.kind == SnapshotKind::Register ? "CPU register"
                 : r.requested.kind == SnapshotKind::Tcb    ? "TCB bytes"
                                                            : "RAM bytes");
            cell(r.requested.label);
            char addr[32];
            std::snprintf(addr, sizeof(addr), "0x%llx",
                          static_cast<unsigned long long>(r.requested.address));
            cell(r.requested.kind == SnapshotKind::Register ? "N/A" : addr);
            cell(r.requested.kind == SnapshotKind::Register ? "N/A" : as_text(r.requested.bytes));
            cell(r.success ? "RECORDED" : "UNKNOWN (read failed)");
            if (!r.success)
                cell("No value");
            else if (r.register_value) {
                std::snprintf(addr, sizeof(addr), "0x%llx",
                              static_cast<unsigned long long>(*r.register_value));
                cell(addr);
            } else {
                static constexpr char digits[] = "0123456789abcdef";
                std::string hex;
                for (std::size_t j = 0; j < std::min<std::size_t>(16, r.data.size()); ++j) {
                    if (j)
                        hex += ' ';
                    hex += digits[r.data[j] >> 4];
                    hex += digits[r.data[j] & 15];
                }
                cell(hex);
                if (ImGui::IsItemHovered() && r.data.size() > 16)
                    ImGui::SetTooltip(
                        "Showing first 16 bytes of %zu recorded; no automatic raw export",
                        r.data.size());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Every successful or failed value is a claim from the receipt producer.");
    ImGui::TextDisabled("Memory permissions, probe health and target/firmware identities require "
                        "ZEW qualification.");
}

void Workspace::draw_live_trace() {
    header("W18 Live trace epochs / observed arrivals");
    ImGui::TextColored(
        ImVec4(1.f, .75f, .43f, 1.f),
        "Source is an unverified local binary spool — not a live debugger or CPU trace.");
    ImGui::TextDisabled(
        "Launch an owned TCP receiver with --spool NEW_FILE, then open that file here.");
    ImGui::TextDisabled(
        "No target halt/probe connection. Unknown task/ISR meanings are NOT interpreted.");
    ImGui::SetNextItemWidth(550.f);
    ImGui::InputText("Binary .rtob file", live_file_path_.data(), live_file_path_.size());
    if (ImGui::Button("Open / restart"))
        (void)open_live_file(live_file_path_.data());
    ImGui::SameLine();
    if (ImGui::Button("Poll now"))
        (void)poll_live_file();
    ImGui::SameLine();
    ImGui::Checkbox("Follow file", &live_follow_);
    if (!live_error_.empty())
        ImGui::TextColored(ImVec4(1.f, .48f, .38f, 1.f), "%s", live_error_.c_str());
    if (!live_session_) {
        ImGui::TextDisabled(
            "No file opened. This panel has no automatic network, RTT or GDB access.");
        return;
    }
    const auto &live = *live_session_;
    const auto &state = live.status();
    const auto &decoder = live.wire();
    ImGui::Text("Read: %llu bytes | %llu frames | %llu observed events | %zu epochs",
                static_cast<unsigned long long>(state.consumed_bytes),
                static_cast<unsigned long long>(state.accepted_frames),
                static_cast<unsigned long long>(state.total_events), live.epochs().size());
    if (state.waiting_for_frame)
        ImGui::TextDisabled("Waiting for remaining bytes of partial frame (not yet accepted)");
    if (state.incomplete)
        ImGui::TextColored(ImVec4(1.f, .74f, .43f, 1.f),
                           "INCOMPLETE / UNKNOWN: losses, corruption or bounded history");
    else
        ImGui::TextDisabled("No detected error yet. Unseen trailing events may still be missing.");
    ImGui::Text("CRC %u | Invalid %u | Replay %u | Frame gaps %u | Attempt gaps %u | Pruned %llu",
                decoder.crc_errors, decoder.invalid_frames, decoder.replayed_frames,
                decoder.missing_frames, decoder.missing_attempts,
                static_cast<unsigned long long>(state.history_pruned));
    if (decoder.loss_saturated)
        ImGui::TextDisabled("Loss counter saturated; precise total is unknown");
    if (!live.epochs().empty()) {
        const auto &epoch = live.epochs().back();
        ImGui::SeparatorText("User-supplied clock calibration (current epoch ONLY)");
        ImGui::Text("Epoch %u — raw timestamp ticks, clock rate unknown until you apply it",
                    epoch.id);
        ImGui::SetNextItemWidth(200.f);
        ImGui::InputScalar("Ticks/second", ImGuiDataType_U32, &live_ticks_per_second_);
        ImGui::SameLine();
        if (ImGui::Button("Apply to current epoch")) {
            if (!epoch.first_tick || !live_session_->set_clock(
                                         {epoch.id, *epoch.first_tick, 0u, live_ticks_per_second_}))
                live_error_ = "Invalid clock rate; requires actual board timing evidence";
            else
                live_error_.clear();
        }
        ImGui::TextDisabled(
            "Times then mean elapsed microseconds since first OBSERVED event, not wall time.");
    }
    if (ImGui::BeginTable("W18 epochs", 5, table_flags, ImVec2(0, 115.f))) {
        for (const char *title : {"Epoch", "Frames", "Events", "Stats", "Integrity"})
            ImGui::TableSetupColumn(title);
        ImGui::TableHeadersRow();
        for (const auto &epoch : live.epochs()) {
            ImGui::TableNextRow();
            cell(std::to_string(epoch.id));
            cell(std::to_string(epoch.frames));
            cell(std::to_string(epoch.events));
            cell(std::to_string(epoch.stats));
            cell(epoch.gap_seen || epoch.bad_evidence ? "INCOMPLETE" : "UNVERIFIED");
        }
        ImGui::EndTable();
    }
    header("Observed arrival order — NOT CPU execution/occupancy");
    const auto &recent = live.recent();
    const auto count = std::min<std::size_t>(recent.size(), 120u);
    const float width = std::max(200.f, ImGui::GetContentRegionAvail().x);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("W18 arrival order", ImVec2(width, 130.f));
    auto *canvas = ImGui::GetWindowDrawList();
    const float left = origin.x + 48.f, right = origin.x + width - 15.f;
    for (unsigned lane = 0; lane < 4; ++lane) {
        const float y = origin.y + 18.f + lane * 28.f;
        canvas->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(84, 95, 114, 140));
        const char *name = lane == 0 ? "L0" : lane == 1 ? "L1" : lane == 2 ? "L2" : "L3";
        canvas->AddText(ImVec2(origin.x + 4.f, y - 8.f), IM_COL32(194, 204, 218, 255), name);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto &p = recent[recent.size() - count + i];
        if (p.event.lane >= 4u)
            continue; // table below covers all 32 lanes
        const float x = left + (right - left) * (count <= 1u ? 0.5f
                                                             : static_cast<float>(i) /
                                                                   static_cast<float>(count - 1u));
        const float y = origin.y + 18.f + p.event.lane * 28.f;
        canvas->AddCircleFilled(ImVec2(x, y), 3.5f,
                                p.gap_before ? IM_COL32(255, 162, 88, 255)
                                             : IM_COL32(93, 194, 241, 255));
    }
    ImGui::TextDisabled("Dots are arrival order. Lanes 4–31 appear in the table only.");
    if (ImGui::BeginTable("W18 recent events", 7, table_flags, ImVec2(0, 300.f))) {
        for (const char *title :
             {"Epoch", "Wire #", "Lane", "Core ID", "Event ID", "Task ID", "Ticks / elapsed us"})
            ImGui::TableSetupColumn(title);
        ImGui::TableHeadersRow();
        const auto visible = std::min<std::size_t>(recent.size(), 150u);
        for (std::size_t i = 0; i < visible; ++i) {
            const auto &item = recent[recent.size() - 1u - i];
            ImGui::TableNextRow();
            cell(std::to_string(item.epoch));
            cell(std::to_string(item.frame_sequence));
            cell(std::to_string(item.event.lane));
            cell(std::to_string(item.event.event.core_id));
            cell(std::to_string(item.event.event.event_type));
            cell(std::to_string(item.event.event.task_id));
            const auto converted = live.tick_us(item.epoch, item.event.event.timestamp);
            cell(std::to_string(item.event.event.timestamp) +
                 (converted ? " / " + std::to_string(*converted) : " / unknown"));
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("W16 numeric event IDs need board/RTOS adapter metadata before CPU load or "
                        "scheduler inference.");
}

void Workspace::draw_findings() {
    if (!controller_.loaded())
        return;
    header("Observed findings");
    ImGui::TextDisabled("Patterns in the trace are not confirmed root causes.");
    if (ImGui::BeginTable("findings", 6, table_flags, ImVec2(0, -1))) {
        for (const char *col :
             {"Code", "Confidence", "Task", "Resource", "Observed (us)", "Description"})
            ImGui::TableSetupColumn(col);
        ImGui::TableHeadersRow();
        for (const auto &f : controller_.report().findings) {
            ImGui::TableNextRow();
            cell(f.code);
            cell(f.confidence);
            cell(f.task);
            cell(f.resource);
            cell(as_text(f.observed_us));
            cell(f.description);
        }
        ImGui::EndTable();
    }
}
void Workspace::draw_evidence() {
    if (!controller_.loaded())
        return;
    const auto &decoded = controller_.trace();
    const auto &evidence = decoded.evidence;
    header("Trace source and integrity");
    for (const auto &[name, value] : std::vector<std::pair<std::string, std::string>>{
             {"Path", controller_.origin().string()},
             {"Mode", controller_.mode()},
             {"Decoder", decoded.decoder},
             {"Firmware label (not attested)", decoded.identity.firmware},
             {"RTOS label", decoded.identity.rtos},
             {"Clock label", decoded.identity.clock},
             {"Ticks per second", as_text(decoded.identity.ticks_per_second)},
             {"Sequence available", evidence.sequence_available ? "yes" : "no"},
             {"Missing events", as_text(evidence.missing_events)},
             {"Declared drops", as_text(evidence.declared_drops)},
             {"Analyzer complete", controller_.report().complete ? "yes" : "no"}}) {
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::SameLine(270);
        label(value);
    }
    header("Export investigation report");
    ImGui::SetNextItemWidth(540);
    ImGui::InputText("##export", export_path_.data(), export_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Export JSON report"))
        (void)export_report(export_path_.data());
    ImGui::TextDisabled("Existing files and immutable session directories cannot be overwritten.");
    ImGui::TextDisabled(
        "Offline normalized evidence only. No live target connection or raw CTF adapter.");
}

bool Workspace::open_ctf_directory(const std::string &path) {
    try {
        const auto root = resolve_ctf_root(file_path(path));
        auto candidate = ctf::open(root, true);
        const auto total = std::filesystem::file_size(root / "channel0_0");
        auto index = ctf::build_analysis_index(candidate);
        ctf_ = std::move(candidate);
        ctf_analysis_ = std::move(index);
        ctf_compare_.reset(); // Comparison belongs to one specific primary capture.
        const auto utf8 = root.u8string();
        ctf_active_directory_.assign(utf8.begin(), utf8.end());
        ctf_last_size_ = total;
        ctf_selected_finding_ = 0;
        ctf_selected_event_ = ctf_->findings.empty() || ctf_->findings.front().evidence.empty()
                                  ? 0
                                  : ctf_->findings.front().evidence.front();
        ctf_error_.clear();
        std::snprintf(ctf_directory_.data(), ctf_directory_.size(), "%s",
                      ctf_active_directory_.c_str());
        active_adapter_ = SourceAdapter::Ctf;
        refresh_unified_view();
        page_ = Page::Overview;
        return true;
    } catch (const std::exception &e) {
        ctf_error_ = e.what();
        if (active_adapter_ == SourceAdapter::Ctf) {
            active_adapter_ = SourceAdapter::None;
            unified_ = ToolSummary{};
        }
        ctf_follow_ = false;
        ctf_.reset(); // Fail closed instead of displaying stale data under a new path.
        ctf_analysis_.reset();
        ctf_compare_.reset();
        ctf_active_directory_.clear();
        ctf_last_size_ = 0;
        return false;
    }
}
bool Workspace::open_ctf_comparison(const std::string &path) {
    try {
        if (!ctf_)
            throw Error("Open the main CTF capture first");
        const auto comparison_root = resolve_ctf_root(file_path(path));
        if (std::filesystem::equivalent(comparison_root, file_path(ctf_active_directory_)))
            throw Error("Before/after must use two independent captures");
        auto candidate = ctf::open(comparison_root);
        ctf_compare_ = std::move(candidate);
        ctf_error_.clear();
        return true;
    } catch (const std::exception &e) {
        ctf_error_ = e.what();
        ctf_compare_.reset();
        return false;
    }
}
void Workspace::follow_ctf() {
    if (!ctf_ || ctf_active_directory_.empty())
        return;
    try {
        const auto channel = file_path(ctf_active_directory_) / "channel0_0";
        if (std::filesystem::is_symlink(channel) || !std::filesystem::is_regular_file(channel))
            throw Error("Follow input disappeared or became a symlink");
        const auto size = std::filesystem::file_size(channel);
        if (size < ctf_last_size_)
            throw Error("CTF channel truncated or replaced; stop following");
        if (size == ctf_last_size_)
            return;
        auto updated = ctf::open(file_path(ctf_active_directory_), true);
        if (updated.events.size() < ctf_->events.size())
            throw Error("CTF event history changed; stopped following");
        // This is a local growing-file poll, not a probe/transport connection.
        auto index = ctf::build_analysis_index(updated);
        ctf_ = std::move(updated);
        ctf_analysis_ = std::move(index);
        refresh_unified_view();
        ctf_last_size_ = size;
        ctf_selected_event_ = std::min(ctf_selected_event_, ctf_->events.size() - 1);
        if (!ctf_->findings.empty())
            ctf_selected_finding_ = std::min(ctf_selected_finding_, ctf_->findings.size() - 1);
        ctf_error_.clear();
    } catch (const std::exception &e) {
        ctf_error_ = e.what();
        ctf_follow_ = false;
    }
}

void Workspace::select_ctf_case(std::size_t index) {
    if (!ctf_ || index >= ctf_->findings.size()) return;
    ctf_selected_finding_ = index;
    const auto& finding = ctf_->findings[index];
    if (!finding.evidence.empty() && finding.evidence.front() < ctf_->events.size())
        ctf_selected_event_ = finding.evidence.front();
}

void Workspace::draw_ctf_explorer() {
    if (!ctf_) return;
    auto& data = *ctf_;

        ImGui::TextColored(ImVec4(.74f, .86f, .97f, 1.f), "01  RESOURCE EXPLORER");
        ImGui::TextDisabled("Choose a case or event; details appear on the right.");
        ImGui::Spacing();
        if (ImGui::BeginTabBar("w20-explorer-tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Cases", nullptr,
                                     ctf_tab_request_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                const char *kinds[] = {"All", "Mutex", "Semaphore", "Queue"};
                for (std::size_t kind = 0; kind < 4; ++kind) {
                    if (kind != 0)
                        ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(kind));
                    if (ImGui::RadioButton(kinds[kind], ctf_kind_filter_ == kind)) {
                        ctf_kind_filter_ = kind;
                        const auto accepts = [&](const ctf::Finding &finding) {
                            return kind == 0 || (kind == 1 && finding.kind == "Mutex") ||
                                   (kind == 2 && finding.kind == "Semaphore") ||
                                   (kind == 3 && finding.kind == "Message queue");
                        };
                        for (std::size_t i = 0; i < data.findings.size(); ++i) {
                            if (accepts(data.findings[i])) {
                                select_ctf_case(i);
                                break;
                            }
                        }
                    }
                    ImGui::PopID();
                }
                if (data.findings.empty()) {
                    ImGui::TextDisabled("No matching resource-blocking callbacks in this capture.");
                } else if (ImGui::BeginTable("w20-cases", 3, table_flags, ImVec2(0, 0))) {
                    ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch, .56f);
                    ImGui::TableSetupColumn("Wait (ticks)", ImGuiTableColumnFlags_WidthStretch,
                                            .22f);
                    ImGui::TableSetupColumn("Callback", ImGuiTableColumnFlags_WidthStretch, .22f);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < data.findings.size(); ++i) {
                        const auto &item = data.findings[i];
                        if (ctf_kind_filter_ == 1 && item.kind != "Mutex")
                            continue;
                        if (ctf_kind_filter_ == 2 && item.kind != "Semaphore")
                            continue;
                        if (ctf_kind_filter_ == 3 && item.kind != "Message queue")
                            continue;
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(item.title.c_str(), ctf_selected_finding_ == i))
                            select_ctf_case(i);
                        ImGui::TableNextColumn();
                        ImGui::Text("%llu", static_cast<unsigned long long>(item.end_tick -
                                                                            item.start_tick));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(item.complete ? "Observed" : "Unknown");
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Timeline", nullptr,
                                     ctf_tab_request_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::TextDisabled("Bars show raw tick intervals, not CPU usage.");
                if (!data.events.empty() && !data.findings.empty()) {
                    const auto start_tick = data.events.front().tick;
                    const auto end_tick = data.events.back().tick;
                    const auto span = std::max<std::uint64_t>(1, end_tick - start_tick);
                    if (ImGui::BeginTable("w20-timeline", 2,
                                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp |
                                              ImGuiTableFlags_ScrollY,
                                          ImVec2(0, 0))) {
                        ImGui::TableSetupColumn("Resource / duration",
                                                ImGuiTableColumnFlags_WidthStretch, .50f);
                        ImGui::TableSetupColumn("Observed interval",
                                                ImGuiTableColumnFlags_WidthStretch, .50f);
                        ImGui::TableHeadersRow();
                        const std::size_t count = std::min<std::size_t>(data.findings.size(), 200);
                        for (std::size_t i = 0; i < count; ++i) {
                            const auto &item = data.findings[i];
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (ImGui::Selectable(item.title.c_str(), ctf_selected_finding_ == i))
                                select_ctf_case(i);
                            ImGui::TextDisabled("%llu ticks", static_cast<unsigned long long>(
                                                                  item.end_tick - item.start_tick));
                            ImGui::TableNextColumn();
                            const float width = std::max(40.f, ImGui::GetContentRegionAvail().x);
                            ImGui::InvisibleButton("##span", ImVec2(width, 27.f));
                            const auto p0 = ImGui::GetItemRectMin();
                            const auto p1 = ImGui::GetItemRectMax();
                            auto *draw = ImGui::GetWindowDrawList();
                            draw->AddRectFilled(ImVec2(p0.x, p0.y + 6.f), ImVec2(p1.x, p0.y + 21.f),
                                                IM_COL32(31, 43, 59, 255), 3.f);
                            const double x0 = static_cast<double>(item.start_tick - start_tick) /
                                              static_cast<double>(span);
                            const double x1 = static_cast<double>(item.end_tick - start_tick) /
                                              static_cast<double>(span);
                            const float left = p0.x + static_cast<float>(x0) * width;
                            const float right = p0.x + static_cast<float>(x1) * width;
                            const ImU32 color = item.kind == "Mutex" ? IM_COL32(243, 166, 85, 255)
                                                : item.kind == "Semaphore"
                                                    ? IM_COL32(71, 187, 209, 255)
                                                    : IM_COL32(177, 151, 240, 255);
                            draw->AddRectFilled(
                                ImVec2(left, p0.y + 6.f),
                                ImVec2(std::min(p1.x, std::max(left + 3.f, right)), p0.y + 21.f),
                                color, 3.f);
                            if (ctf_selected_finding_ == i)
                                draw->AddRect(ImVec2(left, p0.y + 5.f),
                                              ImVec2(std::min(p1.x, std::max(left + 4.f, right)),
                                                     p0.y + 22.f),
                                              IM_COL32(236, 246, 255, 255), 3.f);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\nRaw wait: %llu ticks", item.title.c_str(),
                                                  static_cast<unsigned long long>(item.end_tick -
                                                                                  item.start_tick));
                            if (ImGui::IsItemClicked())
                                select_ctf_case(i);
                            ImGui::PopID();
                        }
                        if (data.findings.size() > count)
                            ImGui::TextDisabled("Timeline displays first 200 resource cases.");
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Threads / ISR", nullptr,
                                     ctf_tab_request_ == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
                draw_ctf_scheduler();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Events", nullptr,
                                     ctf_tab_request_ == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##w20-filter", ctf_filter_.data(), ctf_filter_.size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Filter event names (first 20,000 events scanned)");
                const auto query = lower(ctf_filter_.data());
                std::vector<std::size_t> matches;
                const std::size_t scan = std::min<std::size_t>(data.events.size(), 20000);
                for (std::size_t i = 0; i < scan && matches.size() < 5000; ++i) {
                    if (query.empty() ||
                        lower(data.events[i].name).find(query) != std::string::npos)
                        matches.push_back(i);
                }
                ImGui::BeginChild("w20-events-scroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(matches.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const auto index = matches[static_cast<std::size_t>(row)];
                        const auto &event = data.events[index];
                        ImGui::PushID(static_cast<int>(index));
                        const auto description = "#" + std::to_string(index) + "  " + event.name +
                                                 "  |  " + std::to_string(event.tick);
                        if (ImGui::Selectable(description.c_str(), ctf_selected_event_ == index))
                            ctf_selected_event_ = index;
                        ImGui::PopID();
                    }
                }
                if (data.events.size() > scan)
                    ImGui::TextDisabled("Event browser scans first 20,000 records.");
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Compare", nullptr,
                                     ctf_tab_request_ == 4 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::TextDisabled("Before / after requires two comparable independent captures.");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##w20-compare", ctf_comparison_.data(), ctf_comparison_.size());
#ifdef _WIN32
                if (ImGui::Button("Browse comparison...")) {
                    if (auto folder = choose_ctf_directory_native()) {
                        std::snprintf(ctf_comparison_.data(), ctf_comparison_.size(), "%s",
                                      folder->c_str());
                        (void)open_ctf_comparison(*folder);
                    }
                }
                ImGui::SameLine();
#endif
                if (ImGui::Button("Load comparison"))
                    (void)open_ctf_comparison(ctf_comparison_.data());
                if (ctf_compare_) {
                    ImGui::Text("Events: %zu  ->  %zu", data.events.size(),
                                ctf_compare_->events.size());
                    ImGui::Text("Resource cases: %zu  ->  %zu", data.findings.size(),
                                ctf_compare_->findings.size());
                }
                ImGui::TextWrapped("Workload, clocks, firmware identity and trace loss must "
                                   "be qualified before comparing performance.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        ctf_tab_request_ = -1;
        }
}

void Workspace::draw_ctf_inspector() {
    if (!ctf_) return;
    auto& data = *ctf_;
    if (!data.findings.empty())
        ctf_selected_finding_ = std::min(ctf_selected_finding_, data.findings.size()-1);
    if (!data.events.empty())
        ctf_selected_event_ = std::min(ctf_selected_event_, data.events.size()-1);

        ImGui::TextColored(ImVec4(.74f, .86f, .97f, 1.f), "02  EVIDENCE INSPECTOR");
        ImGui::TextDisabled("Selection stays visible while exploring.");
        ImGui::Separator();
        if (!data.findings.empty()) {
            const auto &item = data.findings[ctf_selected_finding_];
            ImGui::TextColored(ImVec4(.97f, .91f, .82f, 1.f), "%s", item.title.c_str());
            ImGui::Text("Observed window: %llu raw ticks",
                        static_cast<unsigned long long>(item.end_tick - item.start_tick));
            ImGui::TextDisabled("%s", item.complete ? "Matched successful callback"
                                                    : "Completion not proven");
            ImGui::Spacing();
            ImGui::TextWrapped("%s", item.assessment.c_str());
            ImGui::SeparatorText("Linked evidence");
            for (const auto index : item.evidence) {
                if (index >= data.events.size())
                    continue;
                const auto &event = data.events[index];
                ImGui::PushID(static_cast<int>(index));
                const auto description = "#" + std::to_string(index) + "  " + event.name +
                                         "  |  tick " + std::to_string(event.tick);
                if (ImGui::Selectable(description.c_str(), ctf_selected_event_ == index))
                    ctf_selected_event_ = index;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Source byte offset: %zu", event.byte_offset);
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("No resource case selected.");
        }
        if (data.events.empty())
            return;
        ImGui::SeparatorText("Selected source event");
        if (ImGui::SmallButton("Previous") && ctf_selected_event_ > 0)
            --ctf_selected_event_;
        ImGui::SameLine();
        if (ImGui::SmallButton("Next") && ctf_selected_event_ + 1 < data.events.size())
            ++ctf_selected_event_;
        const auto &event = data.events[ctf_selected_event_];
        ImGui::TextWrapped("#%zu  %s", ctf_selected_event_, event.name.c_str());
        ImGui::Text("Tick: %llu   |   File byte: %zu", static_cast<unsigned long long>(event.tick),
                    event.byte_offset);
        if (event.actor >= 0)
            ImGui::Text("Observed thread: %lld", static_cast<long long>(event.actor));
        else
            ImGui::TextDisabled("Thread identity not observed");
        if (ImGui::BeginTable("w20-field-details", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, .42f);
            ImGui::TableSetupColumn("Recorded value", ImGuiTableColumnFlags_WidthStretch, .58f);
            for (const auto &[key, value] : event.fields) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Correlation does not prove causality or CPU load.");
}


void Workspace::draw_ctf_scheduler() {
    if (!ctf_ || !ctf_analysis_) {
        ImGui::TextDisabled("No qualified CTF scheduling callbacks available.");
        return;
    }
    const auto& model = *ctf_analysis_;
    const auto& events = ctf_->events;
    ImGui::TextColored(ImVec4(.73f, .86f, .97f, 1.f), "CTF SCHEDULING / INTERRUPT OBSERVATIONS");
    ImGui::TextDisabled("Recorded switch/ISR pairs only. Not CPU load or real-time latency.");
    ImGui::Text("Thread spans: %zu    ISR spans: %zu",
                model.thread_spans.size(), model.isr_spans.size());
    if (model.unpaired_thread_switches || model.unpaired_isr_callbacks || model.truncated)
        ImGui::TextColored(ImVec4(1.f, .78f, .43f, 1.f),
                           "Unpaired: thread %zu, ISR %zu%s",
                           model.unpaired_thread_switches, model.unpaired_isr_callbacks,
                           model.truncated ? " | INDEX TRUNCATED" : "");
    if (ImGui::BeginTable("ctf-linked-scheduling", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, .13f);
        ImGui::TableSetupColumn("Actor", ImGuiTableColumnFlags_WidthStretch, .37f);
        ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthStretch, .23f);
        ImGui::TableSetupColumn("Evidence", ImGuiTableColumnFlags_WidthStretch, .27f);
        ImGui::TableHeadersRow();
        const auto draw_spans = [&](const std::vector<ctf::AnalysisSpan>& spans) {
            const std::size_t cap = std::min<std::size_t>(spans.size(), 1000);
            for (std::size_t i = 0; i < cap; ++i) {
                const auto& span = spans[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(span.kind.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(span.subject.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(
                    span.end_tick - span.start_tick));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Start")) {
                    ctf_selected_event_ = span.begin_event;
                    ctf_inspector_detached_ = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("End")) {
                    ctf_selected_event_ = span.end_event;
                    ctf_inspector_detached_ = true;
                }
                ImGui::PopID();
            }
        };
        ImGui::PushID("thread-spans");
        draw_spans(model.thread_spans);
        ImGui::PopID();
        ImGui::PushID("isr-spans");
        draw_spans(model.isr_spans);
        ImGui::PopID();
        ImGui::EndTable();
    }
    // The source selector uses native CTF event indices; it never fabricates
    // a normalized microsecond timeline or infers lost callbacks.
    (void)events;
}

void Workspace::draw_ctf_floating() {
    // Detached tools follow the active source: no duplicate connections or
    // CTF-only rendering semantics after selecting a different adapter.
    if (unified_.adapter == AdapterKind::None) return;
    if (ctf_explorer_detached_) {
        ImGui::SetNextWindowSize(ImVec2(780.f, 560.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Resource Explorer##w20-floating-explorer",
                          &ctf_explorer_detached_)) {
            if (ImGui::SmallButton("Return to workspace"))
                ctf_explorer_detached_ = false;
            ImGui::Separator();
            if (ctf_explorer_detached_)
                draw_shared_resources();
        }
        ImGui::End();
    }
    if (ctf_inspector_detached_) {
        ImGui::SetNextWindowSize(ImVec2(570.f, 650.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Evidence Inspector##w20-floating-evidence",
                          &ctf_inspector_detached_)) {
            if (ImGui::SmallButton("Return to workspace"))
                ctf_inspector_detached_ = false;
            ImGui::Separator();
            if (ctf_inspector_detached_)
                draw_shared_evidence();
        }
        ImGui::End();
    }
    if (ctf_scheduler_detached_) {
        ImGui::SetNextWindowSize(ImVec2(820.f, 550.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scheduling Timeline##w20-floating-scheduling",
                          &ctf_scheduler_detached_)) {
            if (ImGui::SmallButton("Close analysis window"))
                ctf_scheduler_detached_ = false;
            ImGui::Separator();
            if (ctf_scheduler_detached_)
                draw_shared_timeline();
        }
        ImGui::End();
    }
}

void Workspace::draw_ctf() {
    ImGui::TextColored(ImVec4(.49f, .76f, .97f, 1.f), "ZEPHYR  /  CTF INVESTIGATION");
    ImGui::SetWindowFontScale(1.22f);
    ImGui::TextUnformatted("Kernel resource analysis");
    ImGui::SetWindowFontScale(1.f);

    // Source controls never live inside a fixed-height scrolling card.
    ImGui::Spacing();
    ImGui::TextDisabled("CAPTURE SOURCE  |  Private local files");
    if (ImGui::BeginTable("w20-source", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Local folder", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##w20-folder", "Capture folder path (metadata + channel0_0)",
                                 ctf_directory_.data(), ctf_directory_.size());
        ImGui::TableNextColumn();
#ifdef _WIN32
        if (ImGui::Button("Browse...")) {
            if (auto folder = choose_ctf_directory_native())
                (void)open_ctf_directory(*folder);
        }
        ImGui::SameLine();
#endif
        if (ImGui::Button("Open / reload"))
            (void)open_ctf_directory(ctf_directory_.data());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            follow_ctf();
        ImGui::EndTable();
    }
    ImGui::Checkbox("Follow growing file", &ctf_follow_);
    ImGui::SameLine();
    ImGui::TextDisabled("Select a folder with metadata and channel0_0.");
    if (!ctf_error_.empty()) {
        ImGui::TextColored(ImVec4(1.f, .52f, .41f, 1.f), "Cannot open: %s", ctf_error_.c_str());
    }
    if (!ctf_) {
        ImGui::Separator();
        ImGui::TextDisabled("Browse a Zephyr CTF capture to view resource timelines and evidence.");
        return;
    }

    auto &data = *ctf_;
    if (!data.findings.empty()) {
        ctf_selected_finding_ = std::min(ctf_selected_finding_, data.findings.size() - 1);
    }
    if (!data.events.empty())
        ctf_selected_event_ = std::min(ctf_selected_event_, data.events.size() - 1);

    ImGui::Separator();
    if (ImGui::BeginTable("w20-kpis", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        small_metric("EVENTS", std::to_string(data.events.size()));
        ImGui::TableNextColumn();
        small_metric("THREADS", std::to_string(data.thread_names.size()));
        ImGui::TableNextColumn();
        small_metric("RESOURCE CASES", std::to_string(data.findings.size()));
        ImGui::TableNextColumn();
        small_metric("PENDING BYTES", std::to_string(data.pending_bytes));
        ImGui::EndTable();
    }
    ImGui::TextColored(ImVec4(.98f, .75f, .44f, 1.f),
                       "Clock not calibrated  |  Event loss unknown  |  Findings are correlations");
    if (ImGui::Button(ctf_explorer_detached_ ? "Explorer detached" : "Detach explorer"))
        ctf_explorer_detached_ = !ctf_explorer_detached_;
    ImGui::SameLine();
    if (ImGui::Button(ctf_inspector_detached_ ? "Evidence detached" : "Detach evidence"))
        ctf_inspector_detached_ = !ctf_inspector_detached_;
    ImGui::SameLine();
    if (ImGui::Button("Open scheduling + ISR"))
        ctf_scheduler_detached_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled("Drag floating window titles to another monitor.");
    if (data.pending_bytes)
        ImGui::TextDisabled("Trailing %zu bytes are not decoded events.", data.pending_bytes);




    // Both attached: side-by-side. Detached panes render in dockable,
    // independently movable platform windows sharing this capture.
    if (!ctf_explorer_detached_ && !ctf_inspector_detached_ &&
        ImGui::GetContentRegionAvail().x >= 840.f) {
        const float height = std::max(330.f, ImGui::GetContentRegionAvail().y - 6.f);
        if (ImGui::BeginTable("w20-two-columns", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_Resizable, ImVec2(0, height))) {
            ImGui::TableSetupColumn("Explorer", ImGuiTableColumnFlags_WidthStretch, .58f);
            ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, .42f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginChild("w20-left-pane", ImVec2(0, height - 10.f), ImGuiChildFlags_None);
            draw_ctf_explorer();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("w20-right-pane", ImVec2(0, height - 10.f),
                              ImGuiChildFlags_None);
            draw_ctf_inspector();
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else {
        if (!ctf_explorer_detached_) {
            ImGui::SeparatorText("Resource explorer");
            draw_ctf_explorer();
        }
        if (!ctf_inspector_detached_) {
            ImGui::SeparatorText("Evidence inspector");
            draw_ctf_inspector();
        }
        if (ctf_explorer_detached_ && ctf_inspector_detached_)
            ImGui::TextDisabled("Both panes are detached. Use Window > Restore embedded CTF panels.");
    }
}



void Workspace::draw_ctf_overview() {
    if (!ctf_) return;
    const auto &capture = *ctf_;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available >= 730.f ? 4 : 2;
    if (ImGui::BeginTable("shared-ctf-kpis", columns,
                          ImGuiTableFlags_SizingStretchSame)) {
        const std::array<std::pair<const char *, std::size_t>, 4> metrics{{
            {"EVENTS", capture.events.size()},
            {"THREADS", capture.thread_names.size()},
            {"RESOURCE CASES", capture.findings.size()},
            {"SCHEDULER SPANS", ctf_analysis_
                ? ctf_analysis_->thread_spans.size() + ctf_analysis_->isr_spans.size() : 0u}
        }};
        for (std::size_t i = 0; i < metrics.size(); ++i) {
            if (i % static_cast<std::size_t>(columns) == 0)
                ImGui::TableNextRow();
            ImGui::TableNextColumn();
            small_metric(metrics[i].first, std::to_string(metrics[i].second));
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(.98f, .75f, .44f, 1.f),
        "Raw ticks only  |  Clock uncalibrated  |  Loss unknown");
    ImGui::SeparatorText("Observed resource callbacks");
    std::array<std::size_t, 3> counts{};
    for (const auto &f : capture.findings) {
        if (f.kind == "Mutex") ++counts[0];
        else if (f.kind == "Semaphore") ++counts[1];
        else if (f.kind == "Message queue") ++counts[2];
    }
    const std::size_t largest = std::max({std::size_t{1}, counts[0], counts[1], counts[2]});
    const char *names[] = {"Mutex", "Semaphore", "Message queue"};
    if (ImGui::BeginTable("shared-ctf-types", 2,
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Object type", ImGuiTableColumnFlags_WidthStretch, .32f);
        ImGui::TableSetupColumn("Observed cases", ImGuiTableColumnFlags_WidthStretch, .68f);
        for (std::size_t i = 0; i < 3; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s (%zu)", names[i], counts[i]);
            ImGui::TableNextColumn();
            ImGui::ProgressBar(static_cast<float>(counts[i]) /
                               static_cast<float>(largest), ImVec2(-1, 0), "");
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Open graphical timeline"))
        page_ = Page::Timeline;
    ImGui::SameLine();
    if (ImGui::Button("Investigate resource cases"))
        page_ = Page::Resources;
    ImGui::SeparatorText("Longest observed callback intervals");
    std::vector<std::size_t> ordered;
    for (std::size_t i = 0; i < capture.findings.size(); ++i)
        ordered.push_back(i);
    std::sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
        const auto &x = capture.findings[a], &y = capture.findings[b];
        return (x.end_tick - x.start_tick) > (y.end_tick - y.start_tick);
    });
    std::uint64_t max_duration = 1;
    for (const auto i : ordered)
        max_duration = std::max(max_duration,
            capture.findings[i].end_tick - capture.findings[i].start_tick);
    const auto count = std::min<std::size_t>(ordered.size(), 8);
    for (std::size_t row = 0; row < count; ++row) {
        const auto i = ordered[row];
        const auto &f = capture.findings[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(f.title.c_str(), ctf_selected_finding_ == i)) {
            select_ctf_case(i);
            page_ = Page::Resources;
        }
        const auto duration = f.end_tick - f.start_tick;
        ImGui::ProgressBar(static_cast<float>(duration) /
                           static_cast<float>(max_duration), ImVec2(-1.f, 0.f),
                           (std::to_string(duration) + " ticks").c_str());
        ImGui::PopID();
    }
    if (ordered.empty())
        ImGui::TextDisabled("No recorded resource-blocking cases.");
    ImGui::TextDisabled("This chart does not establish ownership or causal responsibility.");
}

void Workspace::draw_ctf_timeline() {
    if (!ctf_ || ctf_->events.empty()) {
        ImGui::TextDisabled("No captured event intervals available.");
        return;
    }
    const auto &capture = *ctf_;
    const auto first = capture.events.front().tick;
    const auto last = std::max(first + std::uint64_t{1}, capture.events.back().tick);
    const double width_ticks = static_cast<double>(last - first);
    struct VisualSpan {
        std::uint64_t from{}, to{};
        std::size_t event{}, finding{};
        bool resource{};
        ImU32 colour{};
        std::string detail;
    };
    std::map<std::string, std::vector<VisualSpan>> lanes;
    if (ctf_analysis_) {
        const auto append = [&](const std::vector<ctf::AnalysisSpan> &spans,
                                const char *prefix, ImU32 colour) {
            for (std::size_t i = 0; i < std::min<std::size_t>(spans.size(), 2000); ++i) {
                const auto &span = spans[i];
                lanes[std::string(prefix) + " / " + span.subject].push_back({
                    span.start_tick, span.end_tick, span.end_event, 0, false,
                    colour, span.subject
                });
            }
        };
        append(ctf_analysis_->thread_spans, "Thread", IM_COL32(68, 190, 226, 255));
        append(ctf_analysis_->isr_spans, "ISR", IM_COL32(246, 182, 87, 255));
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(capture.findings.size(), 500); ++i) {
        const auto &finding = capture.findings[i];
        const ImU32 colour = finding.kind == "Mutex" ? IM_COL32(255, 164, 85, 255)
            : finding.kind == "Semaphore" ? IM_COL32(82, 203, 177, 255)
            : IM_COL32(172, 145, 236, 255);
        lanes["Resource / " + finding.kind].push_back({
            finding.start_tick, finding.end_tick,
            finding.evidence.empty() ? 0u : finding.evidence.front(),
            i, true, colour, finding.title
        });
    }
    ImGui::TextDisabled("Observed intervals on the original CTF tick axis. No CPU load inferred.");
    ImGui::Text("Tick window: %llu - %llu",
                static_cast<unsigned long long>(first),
                static_cast<unsigned long long>(last));
    if (ctf_analysis_ && (ctf_analysis_->unpaired_thread_switches ||
                          ctf_analysis_->unpaired_isr_callbacks))
        ImGui::TextColored(ImVec4(1.f, .78f, .46f, 1.f),
             "Unpaired callbacks: %zu thread / %zu ISR",
             ctf_analysis_->unpaired_thread_switches,
             ctf_analysis_->unpaired_isr_callbacks);
    if (ImGui::BeginTable("shared-ctf-timeline", 2,
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                         ImGuiTableFlags_Resizable,
                         ImVec2(0.f, std::max(220.f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("Observed actor / object",
                                ImGuiTableColumnFlags_WidthStretch, .32f);
        ImGui::TableSetupColumn("Raw tick intervals",
                                ImGuiTableColumnFlags_WidthStretch, .68f);
        ImGui::TableHeadersRow();
        std::size_t shown = 0;
        for (const auto &[name, spans] : lanes) {
            if (++shown > 50) break;
            ImGui::PushID(static_cast<int>(shown));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", name.c_str());
            ImGui::TableNextColumn();
            const float w = std::max(70.f, ImGui::GetContentRegionAvail().x);
            const float h = std::max(24.f, ImGui::GetFrameHeight() + 6.f);
            ImGui::InvisibleButton("##tick-lane", ImVec2(w, h));
            const ImVec2 p = ImGui::GetItemRectMin();
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ImVec2(p.x, p.y + 4.f),
                                ImVec2(p.x + w, p.y + h - 4.f),
                                IM_COL32(31, 42, 58, 255), 3.f);
            for (int grid = 1; grid < 4; ++grid) {
                const float x = p.x + w * static_cast<float>(grid) / 4.f;
                draw->AddLine(ImVec2(x, p.y + 2.f), ImVec2(x, p.y + h - 2.f),
                              IM_COL32(92, 105, 125, 65));
            }
            const auto map_tick = [&](std::uint64_t tick) {
                return p.x + static_cast<float>(
                    (static_cast<double>(std::clamp(tick, first, last) - first) /
                     width_ticks) * static_cast<double>(w));
            };
            for (const auto &span : spans) {
                if (span.to < first || span.from > last) continue;
                const float left = map_tick(span.from);
                const float right = std::min(p.x + w,
                    std::max(left + 3.f, map_tick(std::max(span.from, span.to))));
                draw->AddRectFilled(ImVec2(left, p.y + 6.f),
                                    ImVec2(right, p.y + h - 6.f),
                                    span.colour, 2.f);
                if (ImGui::IsItemHovered()) {
                    const auto mouse = ImGui::GetMousePos();
                    if (mouse.x >= left && mouse.x <= right) {
                        ImGui::SetTooltip("%s\n%llu raw ticks\nSelect to inspect evidence",
                            span.detail.c_str(),
                            static_cast<unsigned long long>(span.to - span.from));
                        if (ImGui::IsItemClicked()) {
                            if (span.resource)
                                select_ctf_case(span.finding);
                            else if (span.event < capture.events.size())
                                ctf_selected_event_ = span.event;
                            page_ = Page::Evidence;
                        }
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (lanes.size() > 50)
        ImGui::TextDisabled("Showing first 50 actor / object lanes.");
    if (lanes.empty())
        ImGui::TextDisabled("No qualified switch, ISR or resource spans were recorded.");
}

void Workspace::draw_ctf_resources() {
    if (!ctf_) return;
    const auto &capture = *ctf_;
    const char *filters[] = {"All", "Mutex", "Semaphore", "Queue"};
    for (std::size_t i = 0; i < 4; ++i) {
        if (i != 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::RadioButton(filters[i], ctf_kind_filter_ == i))
            ctf_kind_filter_ = i;
        ImGui::PopID();
    }
    const auto list = [&]() {
        if (ImGui::BeginTable("shared-ctf-resource-list", 2,
                             ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                             ImVec2(0.f, std::max(230.f, ImGui::GetContentRegionAvail().y)))) {
            ImGui::TableSetupColumn("Observed callback", ImGuiTableColumnFlags_WidthStretch, .62f);
            ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthStretch, .38f);
            ImGui::TableHeadersRow();
            std::uint64_t longest = 1;
            for (const auto &f : capture.findings)
                longest = std::max(longest, f.end_tick - f.start_tick);
            for (std::size_t i = 0; i < capture.findings.size(); ++i) {
                const auto &f = capture.findings[i];
                if ((ctf_kind_filter_ == 1 && f.kind != "Mutex") ||
                    (ctf_kind_filter_ == 2 && f.kind != "Semaphore") ||
                    (ctf_kind_filter_ == 3 && f.kind != "Message queue"))
                    continue;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(f.title.c_str(), ctf_selected_finding_ == i))
                    select_ctf_case(i);
                ImGui::TableNextColumn();
                const auto duration = f.end_tick - f.start_tick;
                ImGui::ProgressBar(static_cast<float>(duration) /
                                   static_cast<float>(longest), ImVec2(-1.f, 0.f),
                                   (std::to_string(duration)).c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%llu uncalibrated ticks",
                                      static_cast<unsigned long long>(duration));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };
    if (ImGui::GetContentRegionAvail().x >= 760.f) {
        if (ImGui::BeginTable("shared-ctf-resource-panes", 2,
                             ImGuiTableFlags_SizingStretchProp |
                             ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Resource cases", ImGuiTableColumnFlags_WidthStretch, .55f);
            ImGui::TableSetupColumn("Selected evidence", ImGuiTableColumnFlags_WidthStretch, .45f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginChild("shared-ctf-resource-list-child",
                              ImVec2(0, std::max(300.f, ImGui::GetContentRegionAvail().y)),
                              ImGuiChildFlags_None);
            list();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("shared-ctf-resource-inspector-child",
                              ImVec2(0, std::max(300.f, ImGui::GetContentRegionAvail().y)),
                              ImGuiChildFlags_None);
            draw_ctf_inspector();
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else if (ImGui::BeginTabBar("shared-ctf-narrow-resource")) {
        if (ImGui::BeginTabItem("Resource cases")) {
            list();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Evidence inspector")) {
            draw_ctf_inspector();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void Workspace::draw_ctf_events() {
    if (!ctf_) return;
    const auto &events = ctf_->events;
    std::map<std::string, std::size_t> counts;
    const std::size_t max_scan = std::min<std::size_t>(events.size(), 20000);
    for (std::size_t i = 0; i < max_scan; ++i)
        ++counts[events[i].name];
    std::vector<std::pair<std::string, std::size_t>> common(counts.begin(), counts.end());
    std::sort(common.begin(), common.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
    const auto peak = common.empty() ? std::size_t{1} : common.front().second;
    ImGui::TextDisabled("Callback distribution from first %zu events (not a rate).", max_scan);
    for (std::size_t i = 0; i < std::min<std::size_t>(common.size(), 6); ++i) {
        ImGui::TextUnformatted(common[i].first.c_str());
        ImGui::ProgressBar(static_cast<float>(common[i].second) /
                           static_cast<float>(peak), ImVec2(-1.f, 0.f),
                           std::to_string(common[i].second).c_str());
    }
    ImGui::SeparatorText("Source events");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##shared-event-filter", "Filter event name",
                             ctf_filter_.data(), ctf_filter_.size());
    const auto query = lower(ctf_filter_.data());
    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < max_scan && matches.size() < 5000; ++i)
        if (query.empty() || lower(events[i].name).find(query) != std::string::npos)
            matches.push_back(i);
    if (ImGui::BeginTable("shared-ctf-event-list", 3,
                         ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV,
                         ImVec2(0.f, std::max(200.f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthStretch, .10f);
        ImGui::TableSetupColumn("Callback", ImGuiTableColumnFlags_WidthStretch, .60f);
        ImGui::TableSetupColumn("Raw tick", ImGuiTableColumnFlags_WidthStretch, .30f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matches.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i = matches[static_cast<std::size_t>(row)];
                const auto &event = events[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", i);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(event.name.c_str(), ctf_selected_event_ == i,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    ctf_selected_event_ = i;
                    page_ = Page::Evidence;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(event.tick));
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (events.size() > max_scan)
        ImGui::TextDisabled("Event browser is bounded to first 20,000 records.");
}

void Workspace::draw_ctf_tool(Page page) {
    if (!ctf_) {
        ImGui::TextDisabled("Select a local Zephyr CTF capture to begin.");
        if (ImGui::Button("Open Zephyr CTF..."))
            request_source(ImportAdapter::ZephyrCtf);
        return;
    }
    const char *caption = "CTF analysis";
    switch (page) {
    case Page::Overview: caption = "Evidence dashboard"; break;
    case Page::Timeline: caption = "Graphical timeline"; break;
    case Page::CpuWaits: caption = "Scheduling observations"; break;
    case Page::Graph: caption = "Resource / evidence relationships"; break;
    case Page::Diagnostics: caption = "Scheduling and ISR"; break;
    case Page::Resources: caption = "Mutex / semaphore / queue"; break;
    case Page::Causality: caption = "Causal evidence limits"; break;
    case Page::Events: caption = "Source event explorer"; break;
    case Page::Findings: caption = "Resource findings"; break;
    case Page::Evidence: caption = "Evidence inspector"; break;
    case Page::Replay: caption = "Recorded event order"; break;
    default: break;
    }
    ImGui::TextColored(ImVec4(.68f, .84f, .99f, 1.f), "%s", caption);
    if (ImGui::Button("Change source..."))
        request_source(ImportAdapter::ZephyrCtf);
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        follow_ctf();
    if (!ctf_error_.empty())
        ImGui::TextWrapped("%s", ctf_error_.c_str());
    const auto name = file_path(ctf_active_directory_).filename().string();
    ImGui::TextDisabled("Active adapter: Zephyr CTF");
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", name.substr(0, 32).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", ctf_active_directory_.c_str());
    ImGui::Separator();
    switch (page) {
    case Page::Overview: draw_ctf_overview(); break;
    case Page::Timeline: draw_ctf_timeline(); break;
    case Page::CpuWaits:
        ImGui::TextColored(ImVec4(1.f, .76f, .44f, 1.f),
                           "CPU utilization cannot be calculated from this capture.");
        draw_ctf_timeline();
        break;
    case Page::Diagnostics: draw_ctf_timeline(); break;
    case Page::Resources: case Page::Findings: draw_ctf_resources(); break;
    case Page::Graph:
        ImGui::TextDisabled("Resource intervals link to source evidence; no inferred causality.");
        draw_ctf_timeline();
        break;
    case Page::Causality:
        ImGui::TextDisabled("Observed matching callbacks do not prove ownership or deadlock.");
        draw_ctf_resources();
        break;
    case Page::Events: draw_ctf_events(); break;
    case Page::Evidence: draw_ctf_inspector(); break;
    case Page::Replay:
        ImGui::TextDisabled("Recorded ordering only. RTOS state replay is not available.");
        draw_ctf_events();
        break;
    case Page::Faults: draw_faults(); break;
    case Page::SourceGraph: draw_source_graph(); break;
    case Page::DebugBridge: draw_debug_bridge(); break;
    case Page::Snapshot: draw_snapshot_receipt(); break;
    case Page::LiveTrace: draw_live_trace(); break;
    default: draw_ctf_overview(); break;
    }
}

void Workspace::draw_page(Page page) {
    // Every adapter uses the SAME dashboard/timeline/resources/event/inspector
    // renderer. The source projection owns evidence and capability semantics.
    switch (page) {
    case Page::Overview:
    case Page::Timeline:
    case Page::CpuWaits:
    case Page::Diagnostics:
    case Page::Resources:
    case Page::Events:
    case Page::Findings:
    case Page::Evidence:
    case Page::Graph:
    case Page::Ctf:
        draw_shared_tool(page);
        return;
    default: break;
    }
    if (active_adapter_ != SourceAdapter::Normalized &&
        (page == Page::Replay || page == Page::Causality)) {
        draw_shared_tool(page); // recorded event order / observed links only
        return;
    }
    if (active_adapter_ != SourceAdapter::Normalized &&
        page == Page::LiveTrace && active_adapter_ != SourceAdapter::Live) {
        ImGui::TextDisabled("No binary event-spool source is active.");
        if (ImGui::Button("Import binary spool"))
            request_source(ImportAdapter::BinarySpool);
        return;
    }
    switch (page) {
    case Page::Overview:
        draw_overview();
        break;
    case Page::Timeline:
        draw_timeline();
        break;
    case Page::CpuWaits:
        draw_cpu();
        break;
    case Page::Graph:
        draw_graph();
        break;
    case Page::Replay:
        draw_replay();
        break;
    case Page::Diagnostics:
        draw_diagnostics();
        break;
    case Page::Resources:
        draw_resources();
        break;
    case Page::Causality:
        draw_causality();
        break;
    case Page::Faults:
        draw_faults();
        break;
    case Page::SourceGraph:
        draw_source_graph();
        break;
    case Page::DebugBridge:
        draw_debug_bridge();
        break;
    case Page::Snapshot:
        draw_snapshot_receipt();
        break;
    case Page::LiveTrace:
        draw_live_trace();
        break;
    case Page::Ctf:
        draw_ctf();
        break;
    case Page::Events:
        draw_events();
        break;
    case Page::Findings:
        draw_findings();
        break;
    case Page::Evidence:
        draw_evidence();
        break;
    }
}
void Workspace::draw_detached_analysis() {
    for (std::size_t i = 0; i < detached_analysis_pages_.size();) {
        const auto page = detached_analysis_pages_[i];
        const char* labels[] = {
            "Overview", "Execution timeline", "CPU and waits", "Causal graph",
            "Time-travel replay", "Scheduling diagnostics", "Resources and waits",
            "Causal diagnostics", "Fault and memory", "Source symbol graph",
            "Debugger lifecycle", "Snapshot receipts", "Live trace epochs",
            "Events", "Findings", "Evidence details", "Zephyr CTF"
        };
        const auto index = static_cast<std::size_t>(page);
        bool keep_open = true;
        const auto title = std::string(labels[index]) + "##rtos-detached-" +
                           std::to_string(index);
        ImGui::SetNextWindowSize(ImVec2(870.f, 590.f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &keep_open)) {
            ImGui::TextDisabled("Detached analysis | shared session | no extra device connection");
            ImGui::Separator();
            if (page != Page::Ctf)
                draw_page(page);
        }
        ImGui::End();
        if (!keep_open)
            detached_analysis_pages_.erase(detached_analysis_pages_.begin() +
                                           static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

void Workspace::draw() {
    if (active_adapter_ == SourceAdapter::Live && live_follow_ && live_session_)
        (void)poll_live_file();
    if (active_adapter_ == SourceAdapter::Ctf && ctf_follow_) {
        const auto now = std::chrono::steady_clock::now();
        if (now - ctf_last_poll_ > std::chrono::milliseconds(500)) {
            ctf_last_poll_ = now;
            follow_ctf();
        }
    }
    const auto *viewport = ImGui::GetMainViewport();
    if (dockspace_id_ != 0) {
        ImGui::SetNextWindowDockID(dockspace_id_, ImGuiCond_FirstUseEver);
    } else {
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
    }
    if (compact_spacing_)
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 4.f));
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (dockspace_id_ == 0)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("RTOS Observatory", nullptr, flags);
    draw_menus();
    const float workspace_width = ImGui::GetContentRegionAvail().x;
    const bool sidebar_fits =
        plan_layout(workspace_width, ImGui::GetFontSize(), show_sidebar_).show_sidebar;
    if (sidebar_fits) {
        draw_sidebar();
        ImGui::SameLine();
    } else {
        // Narrow and resized windows keep the SAME tools accessible without
        // squeezing the evidence panels into an unusable fixed-width column.
        if (ImGui::Button("Sources..."))
            request_source(active_adapter_ == SourceAdapter::Ctf
                           ? ImportAdapter::ZephyrCtf
                           : ImportAdapter::NormalizedTrace);
        if (workspace_width >= 35.f * ImGui::GetFontSize())
            ImGui::SameLine();
        constexpr const char *compact_pages[] = {
            "Dashboard", "Timeline", "CPU / scheduling", "Evidence relationships",
            "Time-travel / order", "Scheduling / ISR", "Resource objects",
            "Causal diagnostics", "Fault / memory", "Source / symbols",
            "Debugger lifecycle", "Snapshot receipts", "Live event epochs",
            "Events", "Findings", "Evidence inspector", "CTF explorer"
        };
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##compact-tool", compact_pages[static_cast<int>(page_)])) {
            for (int k = 0; k < 16; ++k) {
                if (ImGui::Selectable(compact_pages[k], static_cast<int>(page_) == k))
                    page_ = static_cast<Page>(k);
            }
            ImGui::EndCombo();
        }
    }
    const float footer_height = ImGui::GetTextLineHeightWithSpacing() + 14.f;
    ImGui::BeginChild("Investigation", ImVec2(0, -footer_height), ImGuiChildFlags_None);
    constexpr const char *page_labels[] = {
        "Evidence dashboard", "Execution timeline", "CPU / scheduling",
        "Evidence relationships", "Time-travel / event order", "Scheduling / ISR",
        "Resources and waits", "Causal diagnostics", "Fault and memory",
        "Source / symbol graph", "Debugger lifecycle", "Snapshot receipts",
        "Live event epochs", "Event explorer", "Findings", "Evidence inspector",
        "Zephyr CTF"
    };
    ImGui::TextColored(ImVec4(.91f, .96f, 1.f, 1.f), "%s",
                       page_labels[static_cast<int>(page_)]);
    if (ImGui::GetContentRegionAvail().x > 760.f) {
        ImGui::SameLine();
        const char *source = active_adapter_ == SourceAdapter::Ctf ? "Zephyr CTF"
            : active_adapter_ == SourceAdapter::Normalized ? "Universal trace"
            : active_adapter_ == SourceAdapter::Live ? "Binary spool"
            : "No source loaded";
        ImGui::TextDisabled("  |  %s", source);
    }
    ImGui::Separator();
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.f, .53f, .40f, 1.f), "ERROR: %s",
                           error_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss"))
            error_.clear();
    } else if (!notice_.empty() && active_adapter_ == SourceAdapter::Normalized) {
        ImGui::TextDisabled("%s", notice_.c_str());
    }
    if (active_adapter_ == SourceAdapter::None &&
        (page_ == Page::Overview || page_ == Page::Timeline ||
         page_ == Page::CpuWaits || page_ == Page::Resources ||
         page_ == Page::Events || page_ == Page::Findings ||
         page_ == Page::Evidence || page_ == Page::Graph)) {
        ImGui::Spacing();
        ImGui::TextWrapped("Select an RTOS evidence adapter to start a graphical investigation.");
        if (ImGui::Button("Open data source..."))
            request_source(ImportAdapter::ZephyrCtf);
    } else {
        draw_page(page_);
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (workspace_width < 900.f)
        ImGui::TextDisabled("RTOS Observatory v%s", observatory::build_info::version);
    else
        ImGui::TextDisabled("v%s   |   Source %s   |   Configured %s",
                            observatory::build_info::version,
                            observatory::build_info::commit,
                            observatory::build_info::configured_utc);
    draw_dialogs();
    ImGui::End();
    // Separate dockable ImGui windows share the exact same session and
    // selected source event; viewports can move them outside the main OS window.
    draw_ctf_floating();
    draw_detached_analysis();
    if (compact_spacing_)
        ImGui::PopStyleVar();
}
} // namespace observatory::desktop
