// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/causal_diagnostics.hpp"
#include "observatory/debug_bridge.hpp"
#include "observatory/desktop/session_controller.hpp"
#include "observatory/diagnostics.hpp"
#include "observatory/fault_evidence.hpp"
#include "observatory/graph.hpp"
#include "observatory/live_session.hpp"
#include "observatory/replay.hpp"
#include "observatory/resource_analysis.hpp"
#include "observatory/snapshot_receipt.hpp"
#include "observatory/source_index.hpp"
#include "observatory/timeline.hpp"
#include "observatory/zephyr_ctf.hpp"
#include "observatory/ctf_analysis.hpp"
#include "observatory/desktop/unified_view.hpp"
#include "observatory/desktop/ui_layout.hpp"
#include "observatory/desktop/timeline_navigation.hpp"
#include "observatory/desktop/timeline_measurement.hpp"
#include "observatory/desktop/layout_persistence.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace observatory::desktop {
class Workspace final {
  public:
    Workspace();
    // Safe to call without graphics initialization, including headless smoke tests.
    bool open_trace(const std::string &path, const std::string &decoder = "universal-v1");
    bool open_session(const std::string &path);
    bool open_fault_snapshot(const std::string &path);
    bool open_source_index(const std::string &manifest, const std::string &elf,
                           const std::string &source_root);
    bool open_bridge_status(const std::string &path);
    bool open_snapshot_receipt(const std::string &path);
    bool open_live_file(const std::string &path);
    bool open_ctf_directory(const std::string &path);
    bool open_ctf_comparison(const std::string &path);
    const ctf::Session *ctf_session() const noexcept { return ctf_ ? &*ctf_ : nullptr; }
    void show_ctf() noexcept { page_ = Page::Overview; }
    void show_adapters() noexcept {
        workspace_tab_ = WorkspaceTab::Adapters;
        select_adapters_tab_ = true;
    }
    const ctf::AnalysisIndex* ctf_analysis() const noexcept {
        return ctf_analysis_ ? &*ctf_analysis_ : nullptr;
    }
    void show_ctf_detached_smoke() noexcept {
        page_ = Page::Overview;
        ctf_explorer_detached_ = true;
        ctf_inspector_detached_ = true;
        ctf_scheduler_detached_ = true;
    }
    bool poll_live_file();
    bool export_report(const std::string &path);
    void accept_drop(const std::string &path);
    void set_dockspace_id(std::uint32_t id) noexcept { dockspace_id_ = id; }
    void draw();
    void restore_saved_layout();
    void apply_pending_layout();
    float ui_scale() const noexcept { return ui_scale_; }
    void set_ui_scale(float next) noexcept { ui_scale_ = clamp_text_zoom(next); }
    bool exit_requested() const noexcept { return exit_requested_; }
    bool loaded() const noexcept { return controller_.loaded(); }
    const GraphView *graph() const noexcept { return graph_ ? &*graph_ : nullptr; }
    void show_timeline() noexcept { page_ = Page::Timeline; }
    void show_graph() noexcept { page_ = Page::Graph; }
    void show_replay() noexcept { page_ = Page::Replay; }
    void show_diagnostics() noexcept { page_ = Page::Diagnostics; }
    const DiagnosticsView *diagnostics() const noexcept {
        return diagnostics_ ? &*diagnostics_ : nullptr;
    }
    const ResourceView *resources() const noexcept { return resources_ ? &*resources_ : nullptr; }
    void show_resources() noexcept { page_ = Page::Resources; }
    const CausalView *causality() const noexcept { return causality_ ? &*causality_ : nullptr; }
    void show_causality() noexcept { page_ = Page::Causality; }
    void show_faults() noexcept { page_ = Page::Faults; }
    void show_source_graph() noexcept { page_ = Page::SourceGraph; }
    const SourceIndex *symbols() const noexcept { return symbols_ ? &*symbols_ : nullptr; }
    const BridgeObservation *bridge_observation() const noexcept {
        return bridge_observation_ ? &*bridge_observation_ : nullptr;
    }
    void show_debug_bridge() noexcept { page_ = Page::DebugBridge; }
    void show_snapshot_receipt() noexcept { page_ = Page::Snapshot; }
    void show_live_trace() noexcept { page_ = Page::LiveTrace; }
    const LiveSession *live_session() const noexcept {
        return live_session_ ? &*live_session_ : nullptr;
    }
    const SnapshotCapture *snapshot_receipt() const noexcept {
        return snapshot_receipt_ ? &*snapshot_receipt_ : nullptr;
    }
    const SymbolVerification &symbol_verification() const noexcept { return symbol_verification_; }
    const FaultView *fault_view() const noexcept { return fault_view_ ? &*fault_view_ : nullptr; }
    const ReplaySnapshot *replay() const noexcept { return replay_ ? &*replay_ : nullptr; }
    const std::string &error() const noexcept { return error_; }
    const SessionController &controller() const noexcept { return controller_; }
    const TimelineView *timeline() const noexcept { return timeline_ ? &*timeline_ : nullptr; }

  private:
    enum class Page {
        Overview,
        Timeline,
        CpuWaits,
        Graph,
        Replay,
        Diagnostics,
        Resources,
        Causality,
        Faults,
        SourceGraph,
        DebugBridge,
        Snapshot,
        LiveTrace,
        Events,
        Findings,
        Evidence,
        Ctf
    };
    enum class OpenDialog { None, Trace, Session };
    enum class SourceAdapter { None, Normalized, Ctf, Live };
    enum class WorkspaceTab { Analysis, Adapters };
    enum class ImportAdapter { ZephyrCtf, NormalizedTrace, SavedSession, BinarySpool };
    void request_source(ImportAdapter adapter);
    void draw_source_dialog();
    void draw_ctf_tool(Page page);
    void draw_shared_tool(Page page);
    void draw_shared_dashboard();
    void draw_shared_timeline();
    void draw_shared_graph();
    void draw_shared_resources();
    void draw_shared_events();
    void draw_shared_evidence();
    void draw_shared_findings();
    void refresh_unified_view(bool preserve_selection = false);
    void draw_ctf_overview();
    void draw_ctf_timeline();
    void draw_ctf_resources();
    void draw_ctf_events();
    void draw_menus();
    void save_layout();
    void draw_dialogs();
    void update_timeline();
    void update_graph();
    void update_replay();
    void update_diagnostics();
    void update_resources();
    void update_causality();
    void rebuild_event_filter();
    void draw_sidebar();
    void draw_adapters_tab();
    void draw_overview();
    void draw_timeline();
    void draw_cpu();
    void draw_graph();
    void draw_replay();
    void draw_diagnostics();
    void draw_resources();
    void draw_causality();
    void draw_faults();
    void draw_source_graph();
    void draw_debug_bridge();
    void draw_snapshot_receipt();
    void draw_live_trace();
    void draw_ctf();
    void draw_ctf_explorer();
    void draw_ctf_inspector();
    void draw_ctf_scheduler();
    void draw_ctf_floating();
    void draw_page(Page page);
    void draw_detached_analysis();
    void select_ctf_case(std::size_t index);
    void follow_ctf();
    void draw_events();
    void draw_findings();
    void draw_evidence();
    void recent(const std::string &path, const std::string &decoder, bool session);
    void fit();
    void zoom(bool in);
    std::uint32_t dockspace_id_{};
    std::vector<Page> detached_analysis_pages_; // at most 6 independently movable panels
    SessionController controller_;
    std::optional<TimelineView> timeline_;
    std::optional<GraphView> graph_;
    std::optional<ReplaySnapshot> replay_;
    std::optional<DiagnosticsView> diagnostics_;
    std::optional<ResourceView> resources_;
    std::optional<CausalView> causality_;
    std::optional<FaultSnapshot> fault_snapshot_;
    std::optional<FaultView> fault_view_;
    std::optional<LiveSession> live_session_;
    std::optional<ctf::Session> ctf_;
    ToolSummary unified_;
    TimelineViewport timeline_viewport_;
    TimelineMeasurement timeline_measurement_;
    int timeline_clock_source_{}; // explicit source; unknown is never converted
    int timeline_drag_cursor_{-1}; // 0=A, 1=B; held while pointer is active
    int timeline_unit_preference_{}; // 0=auto, 1=source, 2=us, 3=ms, 4=s
    bool timeline_manual_clock_enabled_{};
    std::uint64_t timeline_manual_clock_hz_{}; // user supplied, never clock-qualified
    std::optional<std::size_t> shared_selected_event_;
    std::optional<std::size_t> shared_selected_case_;
    std::array<char, 128> shared_filter_{};
    std::string shared_cached_filter_;
    std::vector<std::size_t> shared_filtered_events_;
    std::optional<ctf::AnalysisIndex> ctf_analysis_;
    bool ctf_explorer_detached_{};
    bool ctf_inspector_detached_{};
    bool ctf_scheduler_detached_{};
    std::optional<ctf::Session> ctf_compare_;
    std::array<char, 2048> ctf_directory_{};
    std::array<char, 2048> ctf_comparison_{};
    std::array<char, 128> ctf_filter_{};
    std::string ctf_active_directory_;
    std::string ctf_error_;
    int ctf_tab_request_{-1}; // CTF analysis navigator request, consumed by explorer
    std::size_t ctf_kind_filter_{}; // 0=all, 1=mutex, 2=semaphore, 3=queue
    std::size_t ctf_selected_finding_{};
    std::size_t ctf_selected_event_{};
    std::uintmax_t ctf_last_size_{};
    bool ctf_follow_{};
    std::chrono::steady_clock::time_point ctf_last_poll_{};
    std::array<char, 2048> live_file_path_{};
    std::uint64_t live_file_offset_{};
    std::string live_active_file_;
    std::uint32_t live_ticks_per_second_{1000000u}; // UI suggestion only, not automatically applied
    std::string live_error_;
    bool live_follow_{false};
    std::optional<SnapshotCapture> snapshot_receipt_;
    std::array<char, 2048> snapshot_receipt_path_{};
    BridgeMonitor bridge_monitor_;
    std::optional<BridgeObservation> bridge_observation_;
    std::array<char, 2048> bridge_file_path_{};
    std::optional<SourceIndex> symbols_;
    SymbolVerification symbol_verification_;
    std::optional<SourceExcerpt> source_excerpt_;
    std::size_t selected_symbol_{};
    std::uint64_t address_lookup_{};
    std::array<char, 2048> symbol_manifest_path_{};
    std::array<char, 2048> symbol_elf_path_{};
    std::array<char, 2048> symbol_source_root_{};
    std::string fault_origin_;
    std::size_t fault_selected_finding_{};
    std::array<char, 2048> fault_path_{};
    std::uint64_t long_mutex_wait_threshold_us_{0};
    std::size_t selected_causal_finding_{};
    std::string resource_selected_;
    std::array<char, 96> resource_filter_{};
    std::map<std::string, std::uint64_t> relative_deadlines_;
    std::array<char, 96> deadline_task_{};
    std::uint64_t deadline_value_us_{1000};
    std::optional<std::size_t> graph_event_boundary_;
    std::uint64_t replay_cursor_{};
    std::optional<std::size_t> focused_event_;
    std::string graph_selected_;
    bool graph_show_evidence_{true};
    float graph_scale_{1.0f};
    float graph_pan_x_{0.0f};
    float graph_pan_y_{0.0f};
    std::string error_;
    std::string notice_;
    Page page_{Page::Overview};
    bool legacy_decoder_{};
    bool show_sidebar_{true};
    bool compact_spacing_{true};
    float ui_scale_{0.85f}; // compact default; 75%-220%, DPI applied by GLFW host
    bool layout_restore_requested_{};
    bool layout_reset_requested_{};
    std::string layout_feedback_;
    WorkspaceTab workspace_tab_{WorkspaceTab::Analysis};
    bool select_adapters_tab_{};
    bool select_analysis_tab_{};
    SourceAdapter active_adapter_{SourceAdapter::None};
    ImportAdapter import_adapter_{ImportAdapter::ZephyrCtf};
    bool source_dialog_requested_{};
    std::array<char, 2048> import_path_{};
    bool exit_requested_{};
    bool show_about_{};
    bool export_dialog_requested_{};
    OpenDialog pending_open_{OpenDialog::None};
    OpenDialog active_open_{OpenDialog::None};
    std::array<char, 2048> path_{};
    std::array<char, 2048> export_path_{};
    std::array<char, 160> filter_{};
    std::array<char, 96> graph_filter_{};
    std::vector<std::size_t> filtered_;
    struct Recent {
        std::string path;
        std::string decoder;
        bool session{};
    };
    std::vector<Recent> recents_;
    std::uint64_t begin_us_{};
    std::uint64_t end_us_{};
};
} // namespace observatory::desktop
