cmake_minimum_required(VERSION 3.20)
project(NaviVision LANGUAGES CXX C)

# macOS 需要 Objective-C++ 支持 (.mm 文件)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# NaviVision 自身使用 C++20，llama.cpp 构建时使用 C++17 避免 char8_t 问题
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # 要求 Windows SDK 10.0.19041.0+（支持 WGC CreateFreeThreaded）
    set(CMAKE_SYSTEM_VERSION 10.0.19041.0)
endif()

# ============================================================
# 第三方库
# ============================================================
# Dear ImGui（通过 vcpkg 安装，macOS 上通过 Homebrew/vcpkg 安装）
find_package(imgui CONFIG REQUIRED)

# llama.cpp — 作为子目录引入，提供 LLM + 多模态推理
# 关闭不需要的选项以加速构建
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/../llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp)

# mtmd (多模态) — 引入 tools/mtmd 子目录
if (NOT LLAMA_INSTALL_VERSION)
    set(LLAMA_INSTALL_VERSION "0.0.0")
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd ${CMAKE_BINARY_DIR}/mtmd EXCLUDE_FROM_ALL)

# macOS: GLFW + libcurl
if(APPLE)
    find_package(glfw3 3.3 REQUIRED)
    find_package(CURL REQUIRED)
endif()

# ============================================================
# 平台相关源文件
# ============================================================

# 跨平台共享源文件
set(NAVI_COMMON_SOURCES
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/core/game_profile.cpp
    src/inference/inference_engine.cpp
    src/inference/vlm_engine.cpp
    src/models/model_manager.cpp
    src/gui/app_gui.cpp
)

if(WIN32)
    set(NAVI_PLATFORM_SOURCES
        src/main.cpp
        src/platform/platform_win32.cpp
        src/capture/window_enumerator.cpp
        src/capture/wgc_capture.cpp
        src/gui/preview_texture_d3d11.cpp
    )
    set(NAVI_PLATFORM_LIBS
        imgui::imgui
        llama
        mtmd
        ggml
        d3d11
        dxgi
        dwmapi
        windowsapp
        wininet
    )
elseif(APPLE)
    set(NAVI_PLATFORM_SOURCES
        src/main_macos.mm
        src/platform/platform_macos.mm
        src/capture/window_enumerator_macos.mm
        src/capture/macos_capture.mm
        src/gui/preview_texture_opengl.cpp
    )
    set(NAVI_PLATFORM_LIBS
        imgui::imgui
        llama
        mtmd
        ggml
        glfw
        CURL::libcurl
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework OpenGL"
        "-framework Cocoa"
    )
endif()

# ============================================================
# 主程序：NaviVision
# ============================================================
if(WIN32)
    add_executable(NaviVision WIN32
        ${NAVI_COMMON_SOURCES}
        ${NAVI_PLATFORM_SOURCES}
    )
else()
    add_executable(NaviVision
        ${NAVI_COMMON_SOURCES}
        ${NAVI_PLATFORM_SOURCES}
    )
endif()

target_include_directories(NaviVision PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

target_link_libraries(NaviVision PRIVATE ${NAVI_PLATFORM_LIBS})

# NaviVision 自身使用 C++20
set_target_properties(NaviVision PROPERTIES CXX_STANDARD 20)

# Objective-C++ 编译支持 (macOS .mm 文件)
if(APPLE)
    set_source_files_properties(
        src/main_macos.mm
        src/platform/platform_macos.mm
        src/capture/window_enumerator_macos.mm
        src/capture/macos_capture.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

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

# ── 构建后操作 ──
if(WIN32)
    # 复制 DLL 到 NaviVision.exe 所在目录
    add_custom_command(TARGET NaviVision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:llama>    $<TARGET_FILE_DIR:NaviVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml>     $<TARGET_FILE_DIR:NaviVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml-base> $<TARGET_FILE_DIR:NaviVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml-cpu>  $<TARGET_FILE_DIR:NaviVision>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mtmd>     $<TARGET_FILE_DIR:NaviVision>
    )
endif()

# 复制游戏配置文件到 profiles 子目录（两平台通用）
add_custom_command(TARGET NaviVision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:NaviVision>/profiles
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/profiles $<TARGET_FILE_DIR:NaviVision>/profiles
)

# ============================================================
# 单元测试：NaviVisionTest（控制台程序）
# ============================================================
set(NAVI_TEST_SOURCES
    src/test/test_phase2.cpp
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/core/game_profile.cpp
    src/inference/inference_engine.cpp
)

if(WIN32)
    list(APPEND NAVI_TEST_SOURCES src/platform/platform_win32.cpp)
elseif(APPLE)
    list(APPEND NAVI_TEST_SOURCES src/platform/platform_macos.mm)
endif()

add_executable(NaviVisionTest ${NAVI_TEST_SOURCES})

target_include_directories(NaviVisionTest PRIVATE src)

if(WIN32)
    target_link_libraries(NaviVisionTest PRIVATE gdi32)
elseif(APPLE)
    target_link_libraries(NaviVisionTest PRIVATE
        "-framework CoreGraphics"
        "-framework CoreFoundation"
    )
endif()

set_target_properties(NaviVisionTest PROPERTIES CXX_STANDARD 20)

if(MSVC)
    target_compile_options(NaviVisionTest PRIVATE /utf-8 /W4)
    target_compile_definitions(NaviVisionTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(APPLE)
    set_source_files_properties(
        src/platform/platform_macos.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

# ============================================================
# VLM 加载测试：VlmLoadTest（需要 llama.cpp 链接）
# ============================================================
set(NAVI_VLM_TEST_SOURCES
    src/test/test_vlm_load.cpp
    src/inference/vlm_engine.cpp
    src/inference/inference_engine.cpp
    src/models/model_manager.cpp
)

if(WIN32)
    list(APPEND NAVI_VLM_TEST_SOURCES src/platform/platform_win32.cpp)
elseif(APPLE)
    list(APPEND NAVI_VLM_TEST_SOURCES src/platform/platform_macos.mm)
endif()

add_executable(VlmLoadTest ${NAVI_VLM_TEST_SOURCES})

target_include_directories(VlmLoadTest PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

if(WIN32)
    target_link_libraries(VlmLoadTest PRIVATE llama mtmd ggml wininet)
elseif(APPLE)
    target_link_libraries(VlmLoadTest PRIVATE llama mtmd ggml CURL::libcurl)
endif()

set_target_properties(VlmLoadTest PROPERTIES CXX_STANDARD 20)

if(MSVC)
    target_compile_options(VlmLoadTest PRIVATE /utf-8 /W4)
    target_compile_definitions(VlmLoadTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(WIN32)
    add_custom_command(TARGET VlmLoadTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:llama>    $<TARGET_FILE_DIR:VlmLoadTest>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml>     $<TARGET_FILE_DIR:VlmLoadTest>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml-base> $<TARGET_FILE_DIR:VlmLoadTest>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ggml-cpu>  $<TARGET_FILE_DIR:VlmLoadTest>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mtmd>     $<TARGET_FILE_DIR:VlmLoadTest>
    )
endif()
