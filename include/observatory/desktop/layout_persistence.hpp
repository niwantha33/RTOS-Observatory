// SPDX-License-Identifier: Apache-2.0
#pragma once

// Manual, local-only workspace layout persistence. No trace, capture path,
// recent-file list, symbol names, event values or session data is serialized.
#include "observatory/desktop/ui_layout.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace observatory::desktop {

struct SavedWorkspaceLayout {
    float text_zoom{0.85f};
    bool compact_spacing{true};
    bool show_navigation{true};
    bool show_controls{true};
    bool detach_resources{};
    bool detach_evidence{};
    bool detach_timeline{};
    std::vector<int> detached_pages;
    std::string imgui_geometry;
};

#ifdef _WIN32
// Return a copied Unicode path: the CRT owns a separately allocated buffer.
// _wdupenv_s avoids deprecated _wgetenv and preserves non-ASCII user names.
inline std::optional<std::filesystem::path> windows_environment_path(const wchar_t *key) {
    wchar_t *value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, key) != 0 || !value)
        return std::nullopt;
    std::filesystem::path path(value);
    std::free(value);
    if (path.empty())
        return std::nullopt;
    return path;
}
#endif

inline std::filesystem::path workspace_layout_file() {
#ifdef _WIN32
    if (const auto root = windows_environment_path(L"LOCALAPPDATA"))
        return *root / L"RTOS-Observatory" / L"workspace.layout";
    if (const auto root = windows_environment_path(L"APPDATA"))
        return *root / L"RTOS-Observatory" / L"workspace.layout";
#else
    if (const auto *root = std::getenv("XDG_CONFIG_HOME"); root && *root)
        return std::filesystem::path(root) / "rtos-observatory" / "workspace.layout";
    if (const auto *root = std::getenv("HOME"); root && *root)
        return std::filesystem::path(root) / ".config" / "rtos-observatory" / "workspace.layout";
#endif
    return {}; // Never silently write into the checkout or a working directory.
}

inline bool is_known_layout_window(std::string_view section) {
    if (section == "[Window][Resource Explorer##w20-floating-explorer]" ||
        section == "[Window][Evidence Inspector##w20-floating-evidence]" ||
        section == "[Window][Scheduling Timeline##w20-floating-scheduling]")
        return true;
    constexpr std::array<std::string_view, 17> labels{{
        "Overview", "Execution timeline", "CPU and waits", "Causal graph",
        "Time-travel replay", "Scheduling diagnostics", "Resources and waits",
        "Causal diagnostics", "Fault and memory", "Source symbol graph",
        "Debugger lifecycle", "Snapshot receipts", "Live trace epochs",
        "Events", "Findings", "Evidence details", "Zephyr CTF"
    }};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const std::string expected = "[Window][" + std::string(labels[i]) +
                                     "##rtos-detached-" + std::to_string(i) + "]";
        if (section == expected)
            return true;
    }
    return false;
}

// Only docking geometry and fixed application window names may enter the file.
// The filter also applies on LOAD; editing the file cannot introduce paths.
inline std::string safe_imgui_geometry(std::string_view raw) {
    if (raw.size() > 128 * 1024)
        return {};
    std::istringstream stream{std::string(raw)};
    std::string line;
    std::string result;
    enum class Section { Skip, Window, Docking };
    Section section = Section::Skip;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() > 1024 || line.find('\0') != std::string::npos)
            return {};
        if (line.starts_with("[")) {
            if (line == "[Docking][Data]")
                section = Section::Docking;
            else if (is_known_layout_window(line))
                section = Section::Window;
            else
                section = Section::Skip;
        } else if (section == Section::Window) {
            const bool allowed = line.starts_with("Pos=") || line.starts_with("Size=") ||
                                 line.starts_with("Collapsed=") || line.starts_with("DockId=") ||
                                 line.starts_with("ViewportId=") ||
                                 line.starts_with("ViewportPos=") ||
                                 line.starts_with("ClassId=");
            if (!allowed)
                continue;
        } else if (section == Section::Docking) {
            if (!line.starts_with("DockSpace ") && !line.starts_with("DockNode "))
                continue;
        } else {
            continue;
        }
        if (section != Section::Skip &&
            std::all_of(line.begin(), line.end(), [](unsigned char c) {
                return c >= 32 && c <= 126;
            })) {
            result += line;
            result.push_back('\n');
        }
    }
    return result;
}

inline bool save_workspace_layout(const std::filesystem::path &destination,
                                  const SavedWorkspaceLayout &layout,
                                  std::string &error) {
    error.clear();
    if (destination.empty()) {
        error = "No user configuration directory available.";
        return false;
    }
    std::error_code ec;
    const bool destination_is_link = std::filesystem::is_symlink(destination, ec);
    // POSIX reports ENOENT for an as-yet-uncreated file or parent folder.
    if (ec == std::errc::no_such_file_or_directory)
        ec.clear();
    if (destination_is_link || ec) {
        error = "Refusing to overwrite a layout symlink.";
        return false;
    }
    const auto geometry = safe_imgui_geometry(layout.imgui_geometry);
    std::ostringstream output;
    output << "RTOS_OBSERVATORY_LAYOUT_V2\n"
           << "zoom=" << clamp_text_zoom(layout.text_zoom) << "\n"
           << "compact=" << (layout.compact_spacing ? 1 : 0) << "\n"
           << "navigation=" << (layout.show_navigation ? 1 : 0) << "\n"
           << "controls=" << (layout.show_controls ? 1 : 0) << "\n"
           << "resources=" << (layout.detach_resources ? 1 : 0) << "\n"
           << "evidence=" << (layout.detach_evidence ? 1 : 0) << "\n"
           << "timeline=" << (layout.detach_timeline ? 1 : 0) << "\n"
           << "pages=";
    for (std::size_t i = 0; i < layout.detached_pages.size() && i < 6; ++i) {
        if (i) output << ',';
        output << layout.detached_pages[i];
    }
    output << "\n--IMGUI--\n" << geometry;
    const std::string content = output.str();
    if (content.size() > 128 * 1024) {
        error = "Workspace layout is too large to save.";
        return false;
    }
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "Cannot create the user configuration directory.";
        return false;
    }
    auto temp = destination;
    temp += ".tmp";
    const bool temp_is_link = std::filesystem::is_symlink(temp, ec);
    if (ec == std::errc::no_such_file_or_directory)
        ec.clear();
    if (temp_is_link || ec) {
        error = "Refusing to overwrite a temporary layout symlink.";
        return false;
    }
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(content.data(), static_cast<std::streamsize>(content.size())) ||
            !out.flush()) {
            error = "Could not write workspace layout.";
            return false;
        }
    }
    std::filesystem::rename(temp, destination, ec);
    if (ec) {
        // Windows cannot rename over an existing file. The complete temporary
        // file has been written and closed before replacing the old version.
        std::error_code remove_error;
        std::filesystem::remove(destination, remove_error);
        if (!remove_error) {
            ec.clear();
            std::filesystem::rename(temp, destination, ec);
        }
    }
    if (ec) {
        error = "Could not replace the saved workspace layout.";
        return false;
    }
    return true;
}

inline std::optional<SavedWorkspaceLayout> load_workspace_layout(
    const std::filesystem::path &source, std::string &error) {
    error.clear();
    if (source.empty())
        return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec) && !ec)
        return std::nullopt;
    if (ec || std::filesystem::is_symlink(source, ec) || ec) {
        error = "Cannot safely read the saved layout.";
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(source, ec);
    if (ec || size > 128 * 1024) {
        error = "Saved layout exceeds the size limit.";
        return std::nullopt;
    }
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error = "Cannot open the saved layout.";
        return std::nullopt;
    }
    std::string line;
    const auto field = [&](std::string_view name) -> std::optional<std::string> {
        if (!std::getline(input, line) || !line.starts_with(name))
            return std::nullopt;
        return line.substr(name.size());
    };
    if (!std::getline(input, line) ||
        (line != "RTOS_OBSERVATORY_LAYOUT_V1" && line != "RTOS_OBSERVATORY_LAYOUT_V2")) {
        error = "Unrecognized workspace layout version.";
        return std::nullopt;
    }
    const bool v2 = line == "RTOS_OBSERVATORY_LAYOUT_V2";
    const auto zoom = field("zoom=");
    const auto compact = field("compact=");
    const auto nav = field("navigation=");
    const auto controls = v2 ? field("controls=") : std::optional<std::string>{"1"};
    const auto resources = field("resources=");
    const auto evidence = field("evidence=");
    const auto timeline = field("timeline=");
    const auto pages = field("pages=");
    if (!zoom || !compact || !nav || !controls || !resources || !evidence || !timeline || !pages ||
        !std::getline(input, line) || line != "--IMGUI--") {
        error = "Incomplete workspace layout.";
        return std::nullopt;
    }
    SavedWorkspaceLayout layout;
    const auto parsed = std::from_chars(zoom->data(), zoom->data() + zoom->size(),
                                        layout.text_zoom);
    if (parsed.ec != std::errc{} || parsed.ptr != zoom->data() + zoom->size() ||
        !std::isfinite(layout.text_zoom) ||
        layout.text_zoom < 0.75f || layout.text_zoom > 2.2f) {
        error = "Invalid workspace font scale.";
        return std::nullopt;
    }
    const auto binary = [](const std::string &value, bool &out) {
        if (value != "0" && value != "1") return false;
        out = value == "1";
        return true;
    };
    if (!binary(*compact, layout.compact_spacing) ||
        !binary(*nav, layout.show_navigation) ||
        !binary(*controls, layout.show_controls) ||
        !binary(*resources, layout.detach_resources) ||
        !binary(*evidence, layout.detach_evidence) ||
        !binary(*timeline, layout.detach_timeline)) {
        error = "Invalid workspace options.";
        return std::nullopt;
    }
    if (!pages->empty()) {
        std::istringstream page_stream(*pages);
        std::string token;
        while (std::getline(page_stream, token, ',')) {
            int page = -1;
            const auto parsed_page = std::from_chars(token.data(),
                                                      token.data() + token.size(), page);
            if (parsed_page.ec != std::errc{} ||
                parsed_page.ptr != token.data() + token.size() ||
                page < 0 || page >= 16 || layout.detached_pages.size() >= 6 ||
                std::find(layout.detached_pages.begin(), layout.detached_pages.end(), page) !=
                    layout.detached_pages.end()) {
                error = "Invalid detached panel list.";
                return std::nullopt;
            }
            layout.detached_pages.push_back(page);
        }
    }
    const std::string geometry{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    layout.imgui_geometry = safe_imgui_geometry(geometry);
    return layout;
}

} // namespace observatory::desktop
