// ============================================================
//  NaviVision — 主入口
//
//  职责：
//  1. 创建 Win32 窗口 + D3D11 设备/交换链
//  2. 初始化 Dear ImGui (Win32 + DX11 后端)
//  3. 加载中文字体
//  4. 创建 FrameBuffer / WgcCapture / AppGui 模块
//  5. 运行主渲染循环
//  6. 退出时清理所有资源
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <memory>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "core/frame_buffer.h"
#include "capture/wgc_capture.h"
#include "inference/inference_engine.h"
#include "gui/app_gui.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ============================================================
//  D3D11 全局资源
// ============================================================
static ID3D11Device*            g_pd3dDevice          = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*          g_pSwapChain          = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// ── 前向声明 ──
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ImGui Win32 消息处理（由 ImGui 后端提供）
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
//  WinMain 入口
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // ── 单实例检查 ──
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\NaviVision_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"NaviVision is already running.", L"NaviVision", MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // ── 注册窗口类 ──
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.style          = CS_CLASSDC;
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName  = L"NaviVision";
    RegisterClassExW(&wc);

    // ── 创建主窗口（始终置顶，方便游戏时使用） ──
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST,                          // 始终置顶
        wc.lpszClassName,
        L"NaviVision - AI Game Vision Assistant",
        WS_OVERLAPPEDWINDOW,
        100, 100, 520, 550,                      // 初始位置与大小
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // ── 初始化 D3D11 ──
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ── 初始化 ImGui ──
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 深色科技风格主题
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 3.0f;
    style.WindowBorderSize = 1.0f;

    // ── 加载中文字体 ──
    // 尝试微软雅黑 → 黑体 → 回退到 ImGui 内置字体
    bool fontLoaded = false;
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    for (const char* path : fontPaths) {
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(
                path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        // 使用 ImGui 默认字体（不支持中文，但程序仍可运行）
        io.Fonts->AddFontDefault();
    }

    // ── 初始化 ImGui 平台/渲染后端 ──
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ── 创建核心模块 ──
    auto frameBuffer = std::make_shared<navi::FrameBuffer>();
    auto capture     = std::make_shared<navi::WgcCapture>(frameBuffer);

    // 推理引擎：初始使用 Mock，用户可通过 Model Settings 下载并加载 VLM 模型
    auto inference = std::make_shared<navi::MockInference>();

    navi::AppGui gui(capture, frameBuffer, inference, g_pd3dDevice, g_pd3dDeviceContext);

    // ── 主渲染循环 ──
    const float clearColor[] = { 0.06f, 0.06f, 0.10f, 1.00f };
    bool running = true;

    while (running) {
        // 处理 Windows 消息
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        // 开始新的 ImGui 帧
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 渲染 NaviVision GUI
        gui.render();

        // 完成 ImGui 渲染
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // 呈现（VSync = 1，节省 GPU 资源）
        g_pSwapChain->Present(1, 0);
    }

    // ── 退出清理 ──
    capture->stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    // 释放单实例互斥锁
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

// ============================================================
//  D3D11 设备与交换链
// ============================================================

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0; // 自动匹配窗口
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)          { g_pSwapChain->Release();          g_pSwapChain          = nullptr; }
    if (g_pd3dDeviceContext)   { g_pd3dDeviceContext->Release();    g_pd3dDeviceContext    = nullptr; }
    if (g_pd3dDevice)          { g_pd3dDevice->Release();           g_pd3dDevice          = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// ============================================================
//  Win32 窗口消息处理
// ============================================================

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 优先让 ImGui 处理输入事件
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(
                0,
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)),
                DXGI_FORMAT_UNKNOWN,
                0);
            CreateRenderTarget();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
