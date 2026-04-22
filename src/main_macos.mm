// ============================================================
//  NaviVision — macOS 入口
//
//  使用 GLFW + OpenGL3 + ImGui 实现 macOS 上的 GUI 渲染。
//  结构与 Windows 版本 (main.cpp, D3D11) 保持一致。
// ============================================================

#include <cstdio>
#include <memory>
#include <sys/file.h>
#include <unistd.h>

#include <CoreGraphics/CoreGraphics.h>

// GLFW + OpenGL
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "platform/platform.h"
#include "core/frame_buffer.h"
#include "capture/macos_capture.h"
#include "inference/inference_engine.h"
#include "gui/app_gui.h"

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int /*argc*/, char** /*argv*/) {
    // ── 单实例检查 ──
    const char* lockPath = "/tmp/navivision.lock";
    int lockFd = open(lockPath, O_CREAT | O_RDWR, 0600);
    if (lockFd == -1 || flock(lockFd, LOCK_EX | LOCK_NB) == -1) {
        fprintf(stderr, "[ERROR] NaviVision is already running.\n");
        if (lockFd != -1) close(lockFd);
        return 1;
    }

    // ── 请求屏幕录制权限（macOS 10.15+） ──
    // CGPreflightScreenCaptureAccess 检查是否已授权
    // CGRequestScreenCaptureAccess 触发系统授权对话框
    if (!CGPreflightScreenCaptureAccess()) {
        CGRequestScreenCaptureAccess();
        fprintf(stderr, "[INFO] Screen recording permission requested.\n");
    }

    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    // macOS 使用 OpenGL 3.2 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(
        520, 550,
        "NaviVision - AI Game Vision Assistant",
        nullptr, nullptr);

    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // 设置窗口浮动在最前面
    glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);

    // ── 初始化 ImGui ──
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 3.0f;
    style.WindowBorderSize = 1.0f;

    // ── 加载中文字体 ──
    bool fontLoaded = false;
    auto fontPaths = navi::platform::getChineseFontPaths();
    for (const auto& path : fontPaths) {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(
                path.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        io.Fonts->AddFontDefault();
    }

    // ── 初始化 ImGui 后端 ──
    const char* glsl_version = "#version 150";
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ── 创建核心模块 ──
    auto frameBuffer = std::make_shared<navi::FrameBuffer>();
    auto capture     = std::make_shared<navi::MacosCapture>(frameBuffer);
    auto inference   = std::make_shared<navi::MockInference>();

    navi::AppGui gui(capture, frameBuffer, inference);

    // ── 主渲染循环 ──
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 开始新的 ImGui 帧
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 渲染 NaviVision GUI
        gui.render();

        // 完成 ImGui 渲染
        ImGui::Render();

        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.06f, 0.06f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── 退出清理 ──
    capture->stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    // 释放单实例锁
    flock(lockFd, LOCK_UN);
    close(lockFd);
    unlink(lockPath);

    return 0;
}
