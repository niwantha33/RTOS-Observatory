// SPDX-License-Identifier: Apache-2.0
#include "workspace.hpp"
#include "observatory_build_info.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>
#include <string_view>

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
    style.WindowRounding=6.f;
    style.ChildRounding=6.f;
    style.FrameRounding=5.f;
    style.GrabRounding=5.f;
    style.FramePadding=ImVec2(8,6);
    style.ItemSpacing=ImVec2(8,7);
    auto* c=style.Colors;
    c[ImGuiCol_WindowBg]=ImVec4(.046f,.078f,.13f,1.f);
    c[ImGuiCol_ChildBg]=ImVec4(.075f,.119f,.19f,1.f);
    c[ImGuiCol_FrameBg]=ImVec4(.13f,.20f,.30f,1.f);
    c[ImGuiCol_Header]=ImVec4(.17f,.34f,.60f,.63f);
    c[ImGuiCol_HeaderHovered]=ImVec4(.21f,.41f,.74f,.85f);
    c[ImGuiCol_Button]=ImVec4(.14f,.34f,.68f,1.f);
    c[ImGuiCol_ButtonHovered]=ImVec4(.20f,.42f,.81f,1.f);
    c[ImGuiCol_TitleBgActive]=ImVec4(.08f,.14f,.22f,1.f);
}
} // namespace
int main(int argc, char** argv) {
    observatory::desktop::Workspace workspace;
    bool smoke_ui=false;
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
    if(argc>=3 && std::string_view(argv[1])=="--smoke-ui") {
        smoke_ui=true;
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
        glClearColor(.046f,.078f,.13f,1.f);
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
    if(smoke_ui) std::puts("W05/W06 Dear ImGui GLFW+OpenGL UI smoke passed");
    return 0;
}
