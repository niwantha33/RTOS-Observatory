// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "observatory_build_info.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {
void glfw_error(int code, const char* description) {
    std::fprintf(stderr, "GLFW %d: %s\n",code,description);
}
void drop(GLFWwindow* window, int count, const char** paths) {
    if (count <= 0 || paths == nullptr || paths[0] == nullptr) return;
    if (auto* app=static_cast<observatory::desktop::Workspace*>(glfwGetWindowUserPointer(window)))
        app->accept_drop(paths[0]);
}
void dark_style() {
    ImGui::StyleColorsDark();
    auto& style=ImGui::GetStyle();
    style.WindowPadding=ImVec2(16.f,14.f);
    style.FramePadding=ImVec2(10.f,7.f);
    style.ItemSpacing=ImVec2(10.f,9.f);
    style.ItemInnerSpacing=ImVec2(8.f,6.f);
    style.WindowRounding=9.f;
    style.ChildRounding=9.f;
    style.PopupRounding=8.f;
    style.FrameRounding=6.f;
    style.GrabRounding=6.f;
    style.ScrollbarRounding=7.f;
    style.WindowBorderSize=0.f;
    style.ChildBorderSize=1.f;
    style.FrameBorderSize=0.f;
    auto* c=style.Colors;
    c[ImGuiCol_Text]=ImVec4(.93f,.95f,.98f,1.f);
    c[ImGuiCol_TextDisabled]=ImVec4(.66f,.72f,.80f,1.f);
    c[ImGuiCol_WindowBg]=ImVec4(.050f,.067f,.094f,1.f);
    c[ImGuiCol_ChildBg]=ImVec4(.074f,.098f,.136f,1.f);
    c[ImGuiCol_PopupBg]=ImVec4(.085f,.108f,.150f,.99f);
    c[ImGuiCol_Border]=ImVec4(.19f,.25f,.33f,.85f);
    c[ImGuiCol_FrameBg]=ImVec4(.120f,.163f,.222f,1.f);
    c[ImGuiCol_FrameBgHovered]=ImVec4(.165f,.215f,.294f,1.f);
    c[ImGuiCol_FrameBgActive]=ImVec4(.193f,.258f,.345f,1.f);
    c[ImGuiCol_Header]=ImVec4(.16f,.34f,.54f,.70f);
    c[ImGuiCol_HeaderHovered]=ImVec4(.20f,.41f,.64f,.86f);
    c[ImGuiCol_HeaderActive]=ImVec4(.22f,.45f,.69f,.93f);
    c[ImGuiCol_Button]=ImVec4(.16f,.36f,.60f,1.f);
    c[ImGuiCol_ButtonHovered]=ImVec4(.22f,.45f,.71f,1.f);
    c[ImGuiCol_ButtonActive]=ImVec4(.16f,.36f,.61f,1.f);
    c[ImGuiCol_TitleBgActive]=ImVec4(.075f,.11f,.16f,1.f);
    c[ImGuiCol_TableRowBgAlt]=ImVec4(.12f,.16f,.21f,.35f);
    c[ImGuiCol_Separator]=ImVec4(.24f,.31f,.40f,.70f);
    c[ImGuiCol_CheckMark]=ImVec4(.45f,.76f,.99f,1.f);
}

// Font files remain user/system supplied: no network fetch or binary font bundle.
// Inter is preferred when installed; Segoe UI / Noto / DejaVu and a larger built-in
// font remain safe fallbacks on Windows and Linux.
std::string setup_fonts(GLFWwindow* window) {
    float scale_x=1.f,scale_y=1.f;
    glfwGetWindowContentScale(window,&scale_x,&scale_y);
    const float scale=std::clamp(std::max(scale_x,scale_y),1.f,2.f);
    ImGui::GetStyle().ScaleAllSizes(scale);
    auto* fonts=ImGui::GetIO().Fonts;
    const auto add_font=[&](const std::filesystem::path& path,const char* name)->bool {
        std::error_code ec;
        if(path.empty() || !std::filesystem::is_regular_file(path,ec)) return false;
        const std::string filename=path.string();
        return fonts->AddFontFromFileTTF(filename.c_str(),17.f*scale)!=nullptr;
    };
    if(const char* custom=std::getenv("OBSERVATORY_UI_FONT"); custom && *custom) {
        if(add_font(std::filesystem::path(custom),"Custom")) return "Custom";
    }
#ifdef _WIN32
    const std::filesystem::path fonts_dir=[] {
        if(const char* win=std::getenv("WINDIR"); win && *win)
            return std::filesystem::path(win)/"Fonts";
        return std::filesystem::path("C:/Windows/Fonts");
    }();
    if(add_font(fonts_dir/"Inter-Regular.ttf","Inter")) return "Inter";
    if(add_font(fonts_dir/"segoeui.ttf","Segoe UI")) return "Segoe UI";
#else
    const std::array<std::pair<const char*,const char*>,6> candidates{{
        {"/usr/share/fonts/truetype/inter/Inter-Regular.ttf","Inter"},
        {"/usr/share/fonts/opentype/inter/Inter-Regular.otf","Inter"},
        {"/usr/local/share/fonts/Inter-Regular.ttf","Inter"},
        {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf","Noto Sans"},
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","DejaVu Sans"},
        {"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf","Liberation Sans"}
    }};
    for(const auto& [path,name]:candidates)
        if(add_font(path,name)) return name;
#endif
    ImFontConfig fallback;
    fallback.SizePixels=17.f*scale;
    fonts->AddFontDefault(&fallback);
    return "Built-in (17px)";
}
} // namespace
int main(int argc, char** argv) {
    observatory::desktop::Workspace workspace;
    bool smoke_ui=false;
    bool smoke_graph=false;
    std::string load_path;
    bool session=false;
    if(argc>=2 && std::string_view(argv[1])=="--version") {
        std::printf("RTOS Observatory %s\nCommit: %s\nBuild configured (UTC): %s\n",
            observatory::build_info::version, observatory::build_info::commit,
            observatory::build_info::configured_utc);
        return 0;
    }
    if(argc>=2 && std::string_view(argv[1])=="--help") {
        std::puts("rtos-observatory-desktop [--trace PATH | --session PATH]\n"
                  "  --version        Version, source commit and UTC build configuration time\n"
                  "  --smoke PATH     Headless decoder/controller/timeline test (no graphics)\n"
                  "  --smoke-ui PATH  Hidden GLFW/OpenGL+ImGui one-frame integration test\n"
                  "  --smoke-graph PATH  Headless W07 graph-model smoke test\n"
                  "  --smoke-ui-graph PATH  Hidden W07 graph-canvas integration test\n"
                  "Drag a trace file onto the window or enter a filename in the sidebar.");
        return 0;
    }
    if(argc>=3 && std::string_view(argv[1])=="--smoke") {
        if(!workspace.open_trace(argv[2])) {
            std::fprintf(stderr,"ImGui model smoke failed: %s\n",workspace.error().c_str());
            return 2;
        }
        const auto* timeline=workspace.timeline();
        if(!timeline || timeline->cores.empty() || !workspace.loaded()) {
            std::fputs("ImGui model smoke failed: no core evidence\n",stderr);
            return 3;
        }
        std::puts("W05/W06 Dear ImGui desktop model smoke passed");
        return 0;
    }
    if(argc>=3 && std::string_view(argv[1])=="--smoke-graph") {
        if(!workspace.open_trace(argv[2]) || !workspace.graph() ||
           workspace.graph()->nodes.empty() || workspace.graph()->edges.empty()) {
            std::fprintf(stderr,"W07 graph model smoke failed: %s\n",workspace.error().c_str());
            return 3;
        }
        std::puts("W07 causal graph model smoke passed");
        return 0;
    }
    if(argc>=3 && (std::string_view(argv[1])=="--smoke-ui" ||
                   std::string_view(argv[1])=="--smoke-ui-graph")) {
        smoke_ui=true;
        smoke_graph=std::string_view(argv[1])=="--smoke-ui-graph";
        load_path=argv[2];
    } else if(argc>=3 && std::string_view(argv[1])=="--trace")
        load_path=argv[2];
    else if(argc>=3 && std::string_view(argv[1])=="--session") {
        load_path=argv[2];
        session=true;
    }
    if(!load_path.empty()) {
        const bool ok=session ? workspace.open_session(load_path) : workspace.open_trace(load_path);
        if(!ok) {
            std::fprintf(stderr,"Cannot load evidence: %s\n",workspace.error().c_str());
            return 2;
        }
    }
    if (smoke_graph) workspace.show_graph();
    glfwSetErrorCallback(glfw_error);
    if(!glfwInit()) { std::fputs("GLFW initialization failed\n",stderr); return 3; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
    if(smoke_ui) glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    const std::string title="RTOS Observatory v"+std::string(observatory::build_info::version);
    GLFWwindow* window=glfwCreateWindow(1280,800,title.c_str(),nullptr,nullptr);
    if(!window) { glfwTerminate(); return 4; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window,&workspace);
    glfwSetDropCallback(window,drop);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename=nullptr; // No user profile or trace identity persisted without consent.
    dark_style();
    const std::string font_name=setup_fonts(window);
    if(!ImGui_ImplGlfw_InitForOpenGL(window,true)) {
        std::fputs("ImGui GLFW backend initialization failed\n",stderr);
        ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return 5;
    }
    if(!ImGui_ImplOpenGL3_Init("#version 130")) {
        std::fputs("ImGui OpenGL backend initialization failed\n",stderr);
        ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();
        glfwDestroyWindow(window);glfwTerminate();return 5;
    }
    int frames=0;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if(!smoke_ui && glfwGetWindowAttrib(window,GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(.05);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        workspace.draw();
        if(workspace.exit_requested()) glfwSetWindowShouldClose(window,GLFW_TRUE);
        ImGui::Render();
        int display_width=0,display_height=0;
        glfwGetFramebufferSize(window,&display_width,&display_height);
        glViewport(0,0,display_width,display_height);
        glClearColor(.050f,.067f,.094f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        if(smoke_ui && ++frames>=1) break;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if(smoke_ui) {
        std::printf("Desktop font: %s\n",font_name.c_str());
        std::puts(smoke_graph ? "W07 graph GLFW+OpenGL UI smoke passed" :
                                "W05/W06 Dear ImGui GLFW+OpenGL UI smoke passed");
    }
    return 0;
}
