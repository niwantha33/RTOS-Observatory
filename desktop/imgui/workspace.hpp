// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/session_controller.hpp"
#include "observatory/timeline.hpp"
#include "observatory/graph.hpp"
#include "observatory/replay.hpp"
#include "observatory/diagnostics.hpp"
#include "observatory/resource_analysis.hpp"
#include "observatory/causal_diagnostics.hpp"
#include "observatory/fault_evidence.hpp"
#include "observatory/source_index.hpp"
#include "observatory/debug_bridge.hpp"
#include "observatory/snapshot_receipt.hpp"
#include "observatory/live_session.hpp"
#include <array>
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
    bool open_trace(const std::string& path, const std::string& decoder = "universal-v1");
    bool open_session(const std::string& path);
    bool open_fault_snapshot(const std::string& path);
    bool open_source_index(const std::string& manifest,const std::string& elf,const std::string& source_root);
    bool open_bridge_status(const std::string& path);
    bool open_snapshot_receipt(const std::string& path);
    bool open_live_file(const std::string& path);
    bool poll_live_file();
    bool export_report(const std::string& path);
    void accept_drop(const std::string& path);
    void draw();
    bool exit_requested() const noexcept { return exit_requested_; }
    bool loaded() const noexcept { return controller_.loaded(); }
    const GraphView* graph() const noexcept { return graph_ ? &*graph_ : nullptr; }
    void show_graph() noexcept { page_ = Page::Graph; }
    void show_replay() noexcept { page_ = Page::Replay; }
    void show_diagnostics() noexcept { page_ = Page::Diagnostics; }
    const DiagnosticsView* diagnostics() const noexcept { return diagnostics_ ? &*diagnostics_ : nullptr; }
    const ResourceView* resources() const noexcept { return resources_ ? &*resources_ : nullptr; }
    void show_resources() noexcept { page_ = Page::Resources; }
    const CausalView* causality() const noexcept { return causality_ ? &*causality_ : nullptr; }
    void show_causality() noexcept { page_ = Page::Causality; }
    void show_faults() noexcept { page_ = Page::Faults; }
    void show_source_graph() noexcept { page_ = Page::SourceGraph; }
    const SourceIndex* symbols() const noexcept { return symbols_ ? &*symbols_ : nullptr; }
    const BridgeObservation* bridge_observation() const noexcept { return bridge_observation_ ? &*bridge_observation_ : nullptr; }
    void show_debug_bridge() noexcept { page_ = Page::DebugBridge; }
    void show_snapshot_receipt() noexcept { page_ = Page::Snapshot; }
    void show_live_trace() noexcept { page_ = Page::LiveTrace; }
    const LiveSession* live_session() const noexcept { return live_session_ ? &*live_session_ : nullptr; }
    const SnapshotCapture* snapshot_receipt() const noexcept { return snapshot_receipt_ ? &*snapshot_receipt_ : nullptr; }
    const SymbolVerification& symbol_verification() const noexcept { return symbol_verification_; }
    const FaultView* fault_view() const noexcept { return fault_view_ ? &*fault_view_ : nullptr; }
    const ReplaySnapshot* replay() const noexcept { return replay_ ? &*replay_ : nullptr; }
    const std::string& error() const noexcept { return error_; }
    const SessionController& controller() const noexcept { return controller_; }
    const TimelineView* timeline() const noexcept { return timeline_ ? &*timeline_ : nullptr; }
private:
    enum class Page { Overview, Timeline, CpuWaits, Graph, Replay, Diagnostics, Resources, Causality, Faults, SourceGraph, DebugBridge, Snapshot, LiveTrace, Events, Findings, Evidence };
    enum class OpenDialog { None, Trace, Session };
    void draw_menus();
    void draw_dialogs();
    void update_timeline();
    void update_graph();
    void update_replay();
    void update_diagnostics();
    void update_resources();
    void update_causality();
    void rebuild_event_filter();
    void draw_sidebar();
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
    void draw_events();
    void draw_findings();
    void draw_evidence();
    void recent(const std::string& path, const std::string& decoder, bool session);
    void fit();
    void zoom(bool in);
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
    std::array<char,2048> live_file_path_{};
    std::uint64_t live_file_offset_{};
    std::uint32_t live_ticks_per_second_{1000000u}; // UI suggestion only, not automatically applied
    std::string live_error_;
    bool live_follow_{false};
    std::optional<SnapshotCapture> snapshot_receipt_;
    std::array<char,2048> snapshot_receipt_path_{};
    BridgeMonitor bridge_monitor_;
    std::optional<BridgeObservation> bridge_observation_;
    std::array<char,2048> bridge_file_path_{};
    std::optional<SourceIndex> symbols_;
    SymbolVerification symbol_verification_;
    std::optional<SourceExcerpt> source_excerpt_;
    std::size_t selected_symbol_{};
    std::uint64_t address_lookup_{};
    std::array<char,2048> symbol_manifest_path_{};
    std::array<char,2048> symbol_elf_path_{};
    std::array<char,2048> symbol_source_root_{};
    std::string fault_origin_;
    std::size_t fault_selected_finding_{};
    std::array<char,2048> fault_path_{};
    std::uint64_t long_mutex_wait_threshold_us_{0};
    std::size_t selected_causal_finding_{};
    std::string resource_selected_;
    std::array<char,96> resource_filter_{};
    std::map<std::string,std::uint64_t> relative_deadlines_;
    std::array<char,96> deadline_task_{};
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
    bool compact_spacing_{};
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
    struct Recent { std::string path; std::string decoder; bool session{}; };
    std::vector<Recent> recents_;
    std::uint64_t begin_us_{};
    std::uint64_t end_us_{};
};
} // namespace observatory::desktop
