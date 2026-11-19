// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/session_controller.hpp"
#include "observatory/timeline.hpp"
#include "observatory/graph.hpp"
#include "observatory/replay.hpp"
#include "observatory/diagnostics.hpp"
#include "observatory/resource_analysis.hpp"
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
    const ReplaySnapshot* replay() const noexcept { return replay_ ? &*replay_ : nullptr; }
    const std::string& error() const noexcept { return error_; }
    const SessionController& controller() const noexcept { return controller_; }
    const TimelineView* timeline() const noexcept { return timeline_ ? &*timeline_ : nullptr; }
private:
    enum class Page { Overview, Timeline, CpuWaits, Graph, Replay, Diagnostics, Resources, Events, Findings, Evidence };
    enum class OpenDialog { None, Trace, Session };
    void draw_menus();
    void draw_dialogs();
    void update_timeline();
    void update_graph();
    void update_replay();
    void update_diagnostics();
    void update_resources();
    void rebuild_event_filter();
    void draw_sidebar();
    void draw_overview();
    void draw_timeline();
    void draw_cpu();
    void draw_graph();
    void draw_replay();
    void draw_diagnostics();
    void draw_resources();
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
