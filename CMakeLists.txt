cmake_minimum_required(VERSION 3.20)
project(NaviVision LANGUAGES CXX C)

# NaviVision 自身使用 C++20，llama.cpp 构建时使用 C++17 避免 char8_t 问题
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 要求 Windows SDK 10.0.19041.0+（支持 WGC CreateFreeThreaded）
set(CMAKE_SYSTEM_VERSION 10.0.19041.0)

# ============================================================
# 第三方库
# ============================================================
# Dear ImGui（通过 vcpkg 安装）
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
# EXCLUDE_FROM_ALL 阻止 CLI 可执行文件（需要 common 库）被默认构建
# mtmd 库本身仍会作为 NaviVision 的依赖被构建
if (NOT LLAMA_INSTALL_VERSION)
    set(LLAMA_INSTALL_VERSION "0.0.0")
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd ${CMAKE_BINARY_DIR}/mtmd EXCLUDE_FROM_ALL)

# ============================================================
# 主程序：NaviVision
# ============================================================
add_executable(NaviVision WIN32
    src/main.cpp
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/core/game_profile.cpp
    src/capture/window_enumerator.cpp
    src/capture/wgc_capture.cpp
    src/inference/inference_engine.cpp
    src/inference/vlm_engine.cpp
    src/models/model_manager.cpp
    src/gui/app_gui.cpp
)

target_include_directories(NaviVision PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

target_link_libraries(NaviVision PRIVATE
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

# NaviVision 自身使用 C++20
set_target_properties(NaviVision PROPERTIES CXX_STANDARD 20)

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

# 构建后自动复制 DLL 到 NaviVision.exe 所在目录
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
    # 复制游戏配置文件到 profiles 子目录
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:NaviVision>/profiles
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/profiles $<TARGET_FILE_DIR:NaviVision>/profiles
)

# ============================================================
# 单元测试：NaviVisionTest（控制台程序）
# ============================================================
add_executable(NaviVisionTest
    src/test/test_phase2.cpp
    src/core/frame_buffer.cpp
    src/core/response_parser.cpp
    src/core/game_profile.cpp
    src/inference/inference_engine.cpp
)

target_include_directories(NaviVisionTest PRIVATE src)
target_link_libraries(NaviVisionTest PRIVATE gdi32)

set_target_properties(NaviVisionTest PROPERTIES CXX_STANDARD 20)

if(MSVC)
    target_compile_options(NaviVisionTest PRIVATE /utf-8 /W4)
    target_compile_definitions(NaviVisionTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ============================================================
# VLM 加载测试：VlmLoadTest（需要 llama.cpp 链接）
# ============================================================
add_executable(VlmLoadTest
    src/test/test_vlm_load.cpp
    src/inference/vlm_engine.cpp
    src/inference/inference_engine.cpp
    src/models/model_manager.cpp
)

target_include_directories(VlmLoadTest PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/tools/mtmd
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

target_link_libraries(VlmLoadTest PRIVATE
    llama
    mtmd
    ggml
    wininet
)

set_target_properties(VlmLoadTest PROPERTIES CXX_STANDARD 20)

if(MSVC)
    target_compile_options(VlmLoadTest PRIVATE /utf-8 /W4)
    target_compile_definitions(VlmLoadTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

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
