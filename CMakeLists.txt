cmake_minimum_required(VERSION 3.20)
project(NaviVision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 要求 Windows SDK 10.0.19041.0+（支持 WGC CreateFreeThreaded）
set(CMAKE_SYSTEM_VERSION 10.0.19041.0)

# ============================================================
# 第三方库：Dear ImGui（通过 vcpkg 安装）
# nlohmann/json — header-only, bundled in third_party/
# ============================================================
find_package(imgui CONFIG REQUIRED)

# ============================================================
# 主程序：NaviVision
# ============================================================
add_executable(NaviVision WIN32
    src/main.cpp
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/capture/window_enumerator.cpp
    src/capture/wgc_capture.cpp
    src/inference/inference_engine.cpp
    src/gui/app_gui.cpp
)

target_include_directories(NaviVision PRIVATE src)

target_link_libraries(NaviVision PRIVATE
    imgui::imgui
    d3d11
    dxgi
    dwmapi
    windowsapp
)

# MSVC 编译选项
if(MSVC)
    target_compile_options(NaviVision PRIVATE /utf-8 /W4)
    target_compile_definitions(NaviVision PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

# ============================================================
# 单元测试：NaviVisionTest（控制台程序）
# ============================================================
add_executable(NaviVisionTest
    src/test/test_phase2.cpp
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/inference/inference_engine.cpp
)

target_include_directories(NaviVisionTest PRIVATE src)
target_link_libraries(NaviVisionTest PRIVATE gdi32)

if(MSVC)
    target_compile_options(NaviVisionTest PRIVATE /utf-8 /W4)
    target_compile_definitions(NaviVisionTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
