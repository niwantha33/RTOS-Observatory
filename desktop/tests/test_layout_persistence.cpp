// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/layout_persistence.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using observatory::desktop::SavedWorkspaceLayout;
using observatory::desktop::load_workspace_layout;
using observatory::desktop::safe_imgui_geometry;
using observatory::desktop::save_workspace_layout;

int main() {
    const std::string raw =
        "[Window][Resource Explorer##w20-floating-explorer]\n"
        "Pos=30,45\nSize=750,460\n"
        "[Window][C:\\private\\capture-name\\channel0_0]\n"
        "Pos=123,456\n"
        "[Window][Evidence details##rtos-detached-15]\n"
        "Pos=65,66\n"
        "[Docking][Data]\nDockSpace ID=0xABC Pos=0,0 Size=200,100\n"
        "[Table][C:\\private\\somewhere]\nColumn 0  Width=250\n";
    const auto safe = safe_imgui_geometry(raw);
    assert(safe.find("Resource Explorer") != std::string::npos);
    assert(safe.find("Evidence details") != std::string::npos);
    assert(safe.find("DockSpace") != std::string::npos);
    assert(safe.find("capture-name") == std::string::npos);
    assert(safe.find("somewhere") == std::string::npos);

    const auto base = std::filesystem::temp_directory_path() /
        ("rtos-observatory-layout-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = base / "workspace.layout";
    SavedWorkspaceLayout source;
    source.text_zoom = 0.85f;
    source.compact_spacing = true;
    source.show_navigation = false;
    source.detach_resources = true;
    source.detach_evidence = true;
    source.detached_pages = {3, 15};
    source.imgui_geometry = raw;
    std::string error;
    assert(save_workspace_layout(path, source, error));
    assert(error.empty());
    const auto loaded = load_workspace_layout(path, error);
    assert(loaded);
    assert(error.empty());
    assert(loaded->text_zoom > 0.84f && loaded->text_zoom < 0.86f);
    assert(loaded->compact_spacing);
    assert(!loaded->show_navigation);
    assert(loaded->detach_resources && loaded->detach_evidence);
    assert(!loaded->detach_timeline);
    assert((loaded->detached_pages == std::vector<int>{3, 15}));
    assert(loaded->imgui_geometry.find("capture-name") == std::string::npos);
    assert(!load_workspace_layout(base / "missing.layout", error));
    assert(error.empty());

    // A failed restore must not apply partial or unsafe settings.
    { std::ofstream output(path, std::ios::trunc); output << "unexpected data"; }
    assert(!load_workspace_layout(path, error));
    assert(!error.empty());
    source.text_zoom = 1.2f;
    assert(save_workspace_layout(path, source, error));
    { std::ofstream output(path, std::ios::trunc);
      output << "RTOS_OBSERVATORY_LAYOUT_V1\nzoom=nan\ncompact=1\nnavigation=1\n"
                "resources=0\nevidence=0\ntimeline=0\npages=\n--IMGUI--\n"; }
    assert(!load_workspace_layout(path, error));
    assert(!error.empty());

    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    return 0;
}
