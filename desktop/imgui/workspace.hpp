// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/session_controller.hpp"
#include "observatory/timeline.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
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
    const std::string& error() const noexcept { return error_; }
    const SessionController& controller() const noexcept { return controller_; }
    const TimelineView* timeline() const noexcept { return timeline_ ? &*timeline_ : nullptr; }
private:
    enum class Page { Overview, Timeline, CpuWaits, Events, Findings, Evidence };
    enum class OpenDialog { None, Trace, Session };
    void draw_menus();
    void draw_dialogs();
    void update_timeline();
    void rebuild_event_filter();
    void draw_sidebar();
    void draw_overview();
    void draw_timeline();
    void draw_cpu();
    void draw_events();
    void draw_findings();
    void draw_evidence();
    void recent(const std::string& path, const std::string& decoder, bool session);
    void fit();
    void zoom(bool in);
    SessionController controller_;
    std::optional<TimelineView> timeline_;
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
    std::vector<std::size_t> filtered_;
    struct Recent { std::string path; std::string decoder; bool session{}; };
    std::vector<Recent> recents_;
    std::uint64_t begin_us_{};
    std::uint64_t end_us_{};
};
} // namespace observatory::desktop
