// SPDX-License-Identifier: Apache-2.0
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "observatory_build_info.hpp"
#include "workspace.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {
void glfw_error(int code, const char *description) {
    std::fprintf(stderr, "GLFW %d: %s\n", code, description);
}
void drop(GLFWwindow *window, int count, const char **paths) {
    if (count <= 0 || paths == nullptr || paths[0] == nullptr)
        return;
    if (auto *app =
            static_cast<observatory::desktop::Workspace *>(glfwGetWindowUserPointer(window)))
        app->accept_drop(paths[0]);
}
void dark_style() {
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.f, 14.f);
    style.FramePadding = ImVec2(9.f, 6.f);
    style.ItemSpacing = ImVec2(9.f, 7.f);
    style.ItemInnerSpacing = ImVec2(8.f, 6.f);
    style.WindowRounding = 9.f;
    style.ChildRounding = 9.f;
    style.PopupRounding = 8.f;
    style.FrameRounding = 6.f;
    style.GrabRounding = 6.f;
    style.ScrollbarRounding = 7.f;
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    auto *c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(.93f, .95f, .98f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(.66f, .72f, .80f, 1.f);
    c[ImGuiCol_WindowBg] = ImVec4(.050f, .067f, .094f, 1.f);
    c[ImGuiCol_ChildBg] = ImVec4(.074f, .098f, .136f, 1.f);
    c[ImGuiCol_PopupBg] = ImVec4(.085f, .108f, .150f, .99f);
    c[ImGuiCol_Border] = ImVec4(.19f, .25f, .33f, .85f);
    c[ImGuiCol_FrameBg] = ImVec4(.120f, .163f, .222f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(.165f, .215f, .294f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(.193f, .258f, .345f, 1.f);
    c[ImGuiCol_Header] = ImVec4(.16f, .34f, .54f, .70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(.20f, .41f, .64f, .86f);
    c[ImGuiCol_HeaderActive] = ImVec4(.22f, .45f, .69f, .93f);
    c[ImGuiCol_Button] = ImVec4(.16f, .28f, .42f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(.22f, .39f, .57f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(.24f, .44f, .64f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(.075f, .11f, .16f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(.12f, .16f, .21f, .35f);
    c[ImGuiCol_Separator] = ImVec4(.24f, .31f, .40f, .70f);
    c[ImGuiCol_CheckMark] = ImVec4(.45f, .76f, .99f, 1.f);
}

// Font files remain user/system supplied: no network fetch or binary font bundle.
// Inter is preferred when installed; Segoe UI / Noto / DejaVu and a larger built-in
// font remain safe fallbacks on Windows and Linux.
std::string setup_fonts(GLFWwindow *window) {
    float scale_x = 1.f, scale_y = 1.f;
    glfwGetWindowContentScale(window, &scale_x, &scale_y);
    const float scale = std::clamp(std::max(scale_x, scale_y), 1.f, 2.f);
    ImGui::GetStyle().ScaleAllSizes(scale);
    auto *fonts = ImGui::GetIO().Fonts;
    const auto add_font = [&](const std::filesystem::path &path, const char *name) -> bool {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec))
            return false;
        const std::string filename = path.string();
        return fonts->AddFontFromFileTTF(filename.c_str(), 17.f * scale) != nullptr;
    };
    if (const char *custom = std::getenv("OBSERVATORY_UI_FONT"); custom && *custom) {
        if (add_font(std::filesystem::path(custom), "Custom"))
            return "Custom";
    }
#ifdef _WIN32
    const std::filesystem::path fonts_dir = [] {
        if (const char *win = std::getenv("WINDIR"); win && *win)
            return std::filesystem::path(win) / "Fonts";
        return std::filesystem::path("C:/Windows/Fonts");
    }();
    if (add_font(fonts_dir / "Inter-Regular.ttf", "Inter"))
        return "Inter";
    if (add_font(fonts_dir / "segoeui.ttf", "Segoe UI"))
        return "Segoe UI";
#else
    const std::array<std::pair<const char *, const char *>, 6> candidates{
        {{"/usr/share/fonts/truetype/inter/Inter-Regular.ttf", "Inter"},
         {"/usr/share/fonts/opentype/inter/Inter-Regular.otf", "Inter"},
         {"/usr/local/share/fonts/Inter-Regular.ttf", "Inter"},
         {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "Noto Sans"},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans"},
         {"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", "Liberation Sans"}}};
    for (const auto &[path, name] : candidates)
        if (add_font(path, name))
            return name;
#endif
    ImFontConfig fallback;
    fallback.SizePixels = 17.f * scale;
    fonts->AddFontDefault(&fallback);
    return "Built-in (17px)";
}
} // namespace
int main(int argc, char **argv) {
    observatory::desktop::Workspace workspace;
    bool smoke_ui = false;
    bool smoke_graph = false;
    bool smoke_replay = false;
    bool smoke_diagnostics = false;
    bool smoke_resources = false;
    bool smoke_causal = false;
    bool smoke_fault = false;
    bool smoke_symbols = false;
    bool smoke_bridge = false;
    bool smoke_snapshot = false;
    bool smoke_live = false;
    bool smoke_ctf = false;
    bool smoke_ctf_detached = false;
    std::string ctf_directory_path;
    std::string live_binary_path;
    std::string snapshot_path;
    std::string bridge_status_path;
    std::string symbol_manifest, symbol_elf, symbol_root;
    std::string fault_load_path;
    std::string load_path;
    bool session = false;
    if (argc >= 2 && std::string_view(argv[1]) == "--version") {
        std::printf("RTOS Observatory %s\nCommit: %s\nBuild configured (UTC): %s\n",
                    observatory::build_info::version, observatory::build_info::commit,
                    observatory::build_info::configured_utc);
        return 0;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--help") {
        std::puts(
            "rtos-observatory-desktop [--trace PATH | --session PATH]\n"
            "  --version        Version, source commit and UTC build configuration time\n"
            "  --smoke PATH     Headless decoder/controller/timeline test (no graphics)\n"
            "  --smoke-ui PATH  Hidden GLFW/OpenGL+ImGui one-frame integration test\n"
            "  --smoke-graph PATH  Headless W07 graph-model smoke test\n"
            "  --smoke-ui-graph PATH  Hidden W07 graph-canvas integration test\n"
            "  --smoke-replay PATH  Headless W08 replay smoke test\n"
            "  --smoke-ui-replay PATH  Hidden W08 replay navigation integration test\n"
            "  --smoke-diagnostics PATH  Headless W09 scheduler diagnostics smoke\n"
            "  --smoke-ui-diagnostics PATH  Hidden W09 diagnostics UI smoke\n"
            "  --smoke-resources PATH  Headless W10 resource semantics smoke\n"
            "  --smoke-ui-resources PATH  Hidden W10 resource panel UI smoke\n"
            "  --smoke-causal PATH  Headless W11 causal pattern smoke\n"
            "  --smoke-ui-causal PATH  Hidden W11 causal evidence UI smoke\n"
            "  --fault-snapshot PATH  View a local, offline W12 snapshot\n"
            "  --smoke-fault PATH  Headless W12 fault snapshot/model smoke\n"
            "  --smoke-ui-fault PATH  Hidden W12 fault evidence panel smoke\n"
            "  --symbols MANIFEST ELF SOURCE_ROOT  Open the offline source/symbol graph\n"
            "  --smoke-symbols MANIFEST ELF SOURCE_ROOT  Headless W13 digest/navigation smoke\n"
            "  --smoke-ui-symbols MANIFEST ELF SOURCE_ROOT  Hidden W13 verified graph UI\n"
            "  --bridge-status FILE  Read a passive Eclipse DSF status publication\n"
            "  --smoke-bridge FILE  Headless W14 lifecycle status smoke\n"
            "  --smoke-ui-bridge FILE  Hidden W14 bridge status panel smoke\n"
            "  --snapshot-receipt FILE  Open unverified W15 saved reads\n"
            "  --smoke-snapshot FILE  Headless W15 receipt model smoke\n"
            "  --smoke-ui-snapshot FILE  Hidden W15 snapshot receipts panel smoke\n"
            "  --live FILE  Follow a local W18 binary .rtob spool\n"
            "  --smoke-live FILE  Headless live session/epoch model smoke\n"
            "  --smoke-ui-live FILE  Hidden W18 live arrival lanes UI smoke\n"
            "  --ctf DIR             Open native Zephyr CTF (metadata + channel0_0)\n"
            "  --smoke-ctf DIR       Headless W20 CTF model smoke\n"
            "  --smoke-ui-ctf DIR    Hidden W20 native GUI render smoke\n"
            "  --smoke-ui-ctf-docking DIR  Hidden detachable panels UI smoke\n"
            "Drag a trace file onto the window or enter a filename in the sidebar.");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-ctf") {
        if (!workspace.open_ctf_directory(argv[2]) || !workspace.ctf_session() ||
            workspace.ctf_session()->events.empty() || !workspace.ctf_analysis()) {
            std::fprintf(stderr, "W20 CTF model smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W20 native Zephyr CTF model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-live") {
        if (!workspace.open_live_file(argv[2]) || !workspace.live_session() ||
            workspace.live_session()->status().total_events != 3u ||
            workspace.live_session()->epochs().size() != 2u) {
            std::fprintf(stderr, "W18 live model smoke failed\n");
            return 3;
        }
        std::puts("W18 synthetic live epoch model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke") {
        if (!workspace.open_trace(argv[2])) {
            std::fprintf(stderr, "ImGui model smoke failed: %s\n", workspace.error().c_str());
            return 2;
        }
        const auto *timeline = workspace.timeline();
        if (!timeline || timeline->cores.empty() || !workspace.loaded()) {
            std::fputs("ImGui model smoke failed: no core evidence\n", stderr);
            return 3;
        }
        std::puts("W05/W06 Dear ImGui desktop model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-graph") {
        if (!workspace.open_trace(argv[2]) || !workspace.graph() ||
            workspace.graph()->nodes.empty() || workspace.graph()->edges.empty()) {
            std::fprintf(stderr, "W07 graph model smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W07 causal graph model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-replay") {
        if (!workspace.open_trace(argv[2]) || !workspace.replay() ||
            workspace.replay()->resources.empty() || !workspace.loaded()) {
            std::fprintf(stderr, "W08 replay smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W08 offline time-travel replay smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-diagnostics") {
        if (!workspace.open_trace(argv[2]) || !workspace.diagnostics() ||
            workspace.diagnostics()->tasks.empty()) {
            std::fprintf(stderr, "W09 diagnostics smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W09 scheduling diagnostics model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-resources") {
        if (!workspace.open_trace(argv[2]) || !workspace.resources() ||
            workspace.resources()->resources.size() < 7 || workspace.resources()->limited) {
            std::fprintf(stderr, "W10 resource model smoke failed: %s\n",
                         workspace.error().c_str());
            return 3;
        }
        std::puts("W10 resource observation model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-causal") {
        if (!workspace.open_trace(argv[2]) || !workspace.causality() ||
            workspace.causality()->findings.empty()) {
            std::fprintf(stderr, "W11 causal model smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W11 source-linked causal diagnostics model smoke passed");
        return 0;
    }
    if (argc >= 5 && std::string_view(argv[1]) == "--smoke-symbols") {
        if (!workspace.open_source_index(argv[2], argv[3], argv[4]) || !workspace.symbols() ||
            workspace.symbols()->functions.size() < 3 ||
            !workspace.symbol_verification().elf_matches ||
            !workspace.symbol_verification().all_sources_match) {
            std::fprintf(stderr, "W13 symbol identity smoke failed: %s\n",
                         workspace.error().c_str());
            return 3;
        }
        std::puts("W13 verified C/C++/Rust symbol graph and source checks passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-bridge") {
        if (!workspace.open_bridge_status(argv[2]) || !workspace.bridge_observation() ||
            !workspace.bridge_observation()->accepted ||
            !workspace.bridge_observation()->record.metadata_only) {
            std::fprintf(stderr, "W14 passive lifecycle smoke failed: %s\n",
                         workspace.error().c_str());
            return 3;
        }
        std::puts("W14 metadata-only DSF owner bridge model smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-snapshot") {
        if (!workspace.open_snapshot_receipt(argv[2]) || !workspace.snapshot_receipt() ||
            workspace.snapshot_receipt()->samples.size() < 5 ||
            workspace.snapshot_receipt()->all_cores_coherent) {
            std::fprintf(stderr, "W15 receipt model smoke failed: %s\n", workspace.error().c_str());
            return 3;
        }
        std::puts("W15 unverified saved RAM/TCB/register receipt smoke passed");
        return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--smoke-fault") {
        if (!workspace.open_fault_snapshot(argv[2]) || !workspace.fault_view() ||
            workspace.fault_view()->findings.empty()) {
            std::fprintf(stderr, "W12 fault snapshot smoke failed: %s\n",
                         workspace.error().c_str());
            return 3;
        }
        std::puts("W12 offline stack, TCB, fault and memory evidence smoke passed");
        return 0;
    }
    if (argc >= 3 &&
        (std::string_view(argv[1]) == "--ctf" || std::string_view(argv[1]) == "--smoke-ui-ctf" ||
         std::string_view(argv[1]) == "--smoke-ui-ctf-docking")) {
        ctf_directory_path = argv[2];
        smoke_ctf = std::string_view(argv[1]) == "--smoke-ui-ctf" ||
                    std::string_view(argv[1]) == "--smoke-ui-ctf-docking";
        smoke_ctf_detached = std::string_view(argv[1]) == "--smoke-ui-ctf-docking";
        smoke_ui = smoke_ctf;
    } else if (argc >= 3 && (std::string_view(argv[1]) == "--live" ||
                             std::string_view(argv[1]) == "--smoke-ui-live")) {
        live_binary_path = argv[2];
        smoke_live = std::string_view(argv[1]) == "--smoke-ui-live";
        smoke_ui = smoke_live;
    } else if (argc >= 3 && (std::string_view(argv[1]) == "--snapshot-receipt" ||
                             std::string_view(argv[1]) == "--smoke-ui-snapshot")) {
        snapshot_path = argv[2];
        smoke_snapshot = std::string_view(argv[1]) == "--smoke-ui-snapshot";
        smoke_ui = smoke_snapshot;
    } else if (argc >= 3 && (std::string_view(argv[1]) == "--bridge-status" ||
                             std::string_view(argv[1]) == "--smoke-ui-bridge")) {
        smoke_bridge = std::string_view(argv[1]) == "--smoke-ui-bridge";
        smoke_ui = smoke_bridge;
        bridge_status_path = argv[2];
    } else if (argc >= 5 && (std::string_view(argv[1]) == "--symbols" ||
                             std::string_view(argv[1]) == "--smoke-ui-symbols")) {
        symbol_manifest = argv[2];
        symbol_elf = argv[3];
        symbol_root = argv[4];
        smoke_symbols = std::string_view(argv[1]) == "--smoke-ui-symbols";
        smoke_ui = smoke_symbols;
    } else if (argc >= 3 && (std::string_view(argv[1]) == "--smoke-ui" ||
                             std::string_view(argv[1]) == "--smoke-ui-graph" ||
                             std::string_view(argv[1]) == "--smoke-ui-replay" ||
                             std::string_view(argv[1]) == "--smoke-ui-diagnostics" ||
                             std::string_view(argv[1]) == "--smoke-ui-resources" ||
                             std::string_view(argv[1]) == "--smoke-ui-causal" ||
                             std::string_view(argv[1]) == "--smoke-ui-fault")) {
        smoke_ui = true;
        smoke_graph = std::string_view(argv[1]) == "--smoke-ui-graph";
        smoke_replay = std::string_view(argv[1]) == "--smoke-ui-replay";
        smoke_diagnostics = std::string_view(argv[1]) == "--smoke-ui-diagnostics";
        smoke_resources = std::string_view(argv[1]) == "--smoke-ui-resources";
        smoke_causal = std::string_view(argv[1]) == "--smoke-ui-causal";
        smoke_fault = std::string_view(argv[1]) == "--smoke-ui-fault";
        if (smoke_fault)
            fault_load_path = argv[2];
        else
            load_path = argv[2];
    } else if (argc >= 3 && std::string_view(argv[1]) == "--fault-snapshot")
        fault_load_path = argv[2];
    else if (argc >= 3 && std::string_view(argv[1]) == "--trace")
        load_path = argv[2];
    else if (argc >= 3 && std::string_view(argv[1]) == "--session") {
        load_path = argv[2];
        session = true;
    }
    if (!load_path.empty()) {
        const bool ok =
            session ? workspace.open_session(load_path) : workspace.open_trace(load_path);
        if (!ok) {
            std::fprintf(stderr, "Cannot load evidence: %s\n", workspace.error().c_str());
            return 2;
        }
    }
    if (smoke_graph)
        workspace.show_graph();
    if (smoke_replay)
        workspace.show_replay();
    if (smoke_diagnostics)
        workspace.show_diagnostics();
    if (smoke_resources)
        workspace.show_resources();
    if (!fault_load_path.empty() && !workspace.open_fault_snapshot(fault_load_path)) {
        std::fprintf(stderr, "Cannot load W12 fault evidence: %s\n", workspace.error().c_str());
        return 2;
    }
    if (!symbol_manifest.empty() &&
        !workspace.open_source_index(symbol_manifest, symbol_elf, symbol_root)) {
        std::fprintf(stderr, "Cannot load W13 symbol map: %s\n", workspace.error().c_str());
        return 2;
    }
    if (!bridge_status_path.empty() && !workspace.open_bridge_status(bridge_status_path)) {
        std::fprintf(stderr, "Cannot load W14 passive bridge status: %s\n",
                     workspace.error().c_str());
        return 2;
    }
    if (!ctf_directory_path.empty() && !workspace.open_ctf_directory(ctf_directory_path)) {
        std::fprintf(stderr, "Cannot open W20 CTF folder\n");
        return 2;
    }
    if (!live_binary_path.empty() && !workspace.open_live_file(live_binary_path)) {
        std::fprintf(stderr, "Cannot open W18 binary spool\n");
        return 2;
    }
    if (!snapshot_path.empty() && !workspace.open_snapshot_receipt(snapshot_path)) {
        std::fprintf(stderr, "Cannot load W15 snapshot receipt: %s\n", workspace.error().c_str());
        return 2;
    }
    if (smoke_causal)
        workspace.show_causality();
    if (smoke_snapshot)
        workspace.show_snapshot_receipt();
    if (smoke_live)
        workspace.show_live_trace();
    if (smoke_ctf)
        workspace.show_ctf();
    if (smoke_ctf_detached)
        workspace.show_ctf_detached_smoke();
    if (smoke_bridge)
        workspace.show_debug_bridge();
    if (smoke_symbols)
        workspace.show_source_graph();
    if (smoke_fault)
        workspace.show_faults();
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) {
        std::fputs("GLFW initialization failed\n", stderr);
        return 3;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    if (smoke_ui)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const std::string title = "RTOS Observatory v" + std::string(observatory::build_info::version);
    GLFWwindow *window = glfwCreateWindow(1280, 800, title.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 4;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window, &workspace);
    glfwSetDropCallback(window, drop);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Official ImGui docking release: panels can dock, float and move to
    // independent OS windows (GLFW multi-viewport backend).
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr; // No user profile or trace identity persisted without consent.
    dark_style();
    const std::string font_name = setup_fonts(window);
    const ImGuiStyle initial_style = ImGui::GetStyle();
    float original_dpi_x = 1.f, original_dpi_y = 1.f;
    glfwGetWindowContentScale(window, &original_dpi_x, &original_dpi_y);
    const float original_dpi = std::max(.5f, std::max(original_dpi_x, original_dpi_y));
    float applied_scale = 1.f;
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::fputs("ImGui GLFW backend initialization failed\n", stderr);
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 5;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        std::fputs("ImGui OpenGL backend initialization failed\n", stderr);
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 5;
    }
    int frames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!smoke_ui && glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(.05);
            continue;
        }
        // Keep font, padding and interaction targets in step with DPI and the
        // user's accessibility scale. Recompute from the pristine style to
        // avoid cumulative scaling during resize or monitor changes.
        float dpi_x = 1.f, dpi_y = 1.f;
        glfwGetWindowContentScale(window, &dpi_x, &dpi_y);
        const float dpi_factor = std::max(.5f, std::max(dpi_x, dpi_y)) / original_dpi;
        // 4K/ultrawide displays at 100% OS scaling need a little more
        // typography than a 1280-pixel window, without sudden size jumps.
        int logical_width = 1280, logical_height = 800;
        glfwGetWindowSize(window, &logical_width, &logical_height);
        const float screen_factor = std::clamp(
            1.f + .14f * std::log2(std::max(1280, logical_width) / 1280.f),
            1.f, 1.30f);
        const float requested_scale = std::clamp(
            dpi_factor * workspace.ui_scale() * screen_factor, .75f, 3.2f);
        if (std::abs(requested_scale - applied_scale) > .01f) {
            ImGui::GetStyle() = initial_style;
            ImGui::GetStyle().ScaleAllSizes(requested_scale);
            ImGui::GetStyle().FontScaleMain = requested_scale;
            applied_scale = requested_scale;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Ctrl+Plus / Ctrl+Minus work without opening Settings. Do not
        // intercept shortcuts while the user is entering a capture path.
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                workspace.set_ui_scale(workspace.ui_scale() + .10f);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                workspace.set_ui_scale(workspace.ui_scale() - .10f);
        }
        // Host a single shared dockspace. All CTF and normalized analyses
        // retain one source/session and can be rearranged independently.
        // No redundant "RTOS Observatory" dock tab when it is the only
        // workspace window; detachable tools keep their normal tabs.
        workspace.set_dockspace_id(ImGui::DockSpaceOverViewport(
            0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar));
        workspace.draw();
        if (workspace.exit_requested())
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        ImGui::Render();
        int display_width = 0, display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        glClearColor(.050f, .067f, .094f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // Render independent platform windows and restore the main GL context.
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(window);
        }
        glfwSwapBuffers(window);
        if (smoke_ui && ++frames >= 1)
            break;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (smoke_ui) {
        std::printf("Desktop font: %s\n", font_name.c_str());
        std::puts(smoke_ctf           ? "W20 native CTF investigation UI smoke passed"
                  : smoke_live        ? "W18 live trace epoch UI smoke passed"
                  : smoke_snapshot    ? "W15 unverified snapshot receipt UI smoke passed"
                  : smoke_bridge      ? "W14 passive DSF bridge UI smoke passed"
                  : smoke_symbols     ? "W13 verified source graph UI smoke passed"
                  : smoke_causal      ? "W11 causal panel UI smoke passed"
                  : smoke_resources   ? "W10 resource panel UI smoke passed"
                  : smoke_diagnostics ? "W09 scheduler UI smoke passed"
                  : smoke_replay      ? "W08 replay GLFW+OpenGL UI smoke passed"
                  : smoke_graph       ? "W07 graph GLFW+OpenGL UI smoke passed"
                                      : "W05/W06 Dear ImGui GLFW+OpenGL UI smoke passed");
    }
    return 0;
}
