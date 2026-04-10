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
# 注意：llama.cpp 使用 C++11/17，需要避免 C++20 的 char8_t 破坏性变更
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/../llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp)

# llava (CLIP + LLaVA 多模态) — 需要手动引入，因为关闭了 examples 构建
add_library(llava OBJECT
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava/llava.cpp
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava/llava.h
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava/clip.cpp
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava/clip.h
)
target_link_libraries(llava PRIVATE ggml llama ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(llava PUBLIC
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)
target_compile_features(llava PRIVATE cxx_std_11)
if(MSVC)
    target_compile_options(llava PRIVATE /utf-8 /W3)
endif()

add_library(llava_static STATIC $<TARGET_OBJECTS:llava>)
target_include_directories(llava_static PUBLIC
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

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
    src/inference/vlm_engine.cpp
    src/models/model_manager.cpp
    src/gui/app_gui.cpp
)

target_include_directories(NaviVision PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/../llama.cpp/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/ggml/include
    ${CMAKE_SOURCE_DIR}/../llama.cpp/examples/llava
    ${CMAKE_SOURCE_DIR}/../llama.cpp/common
)

target_link_libraries(NaviVision PRIVATE
    imgui::imgui
    llama
    llava_static
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
        $<TARGET_FILE:llama> $<TARGET_FILE_DIR:NaviVision>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ggml>  $<TARGET_FILE_DIR:NaviVision>
)

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

set_target_properties(NaviVisionTest PROPERTIES CXX_STANDARD 20)

if(MSVC)
    target_compile_options(NaviVisionTest PRIVATE /utf-8 /W4)
    target_compile_definitions(NaviVisionTest PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
