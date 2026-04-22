# NaviVision

**NaviVision** 是一款基于视觉语言模型 (VLM) 的实时游戏画面分析工具。它会周期性捕获指定窗口的画面，送入本地运行的多模态大模型（默认 Qwen2.5-VL 系列），输出结构化的 JSON 分析结果（当前状态、检测到的单位、战术建议、置信度等），并在 ImGui 界面中实时呈现。

所有推理在本地完成，基于 [llama.cpp](https://github.com/ggml-org/llama.cpp) + mtmd 多模态后端，无需联网。

> English version: see [README.md](README.md)

---

## 功能特性

- **跨平台窗口捕获**
  - Windows：Windows Graphics Capture (WGC) API
  - macOS：ScreenCaptureKit / CGWindowList
- **本地 VLM 推理**：通过 llama.cpp + mtmd 加载 GGUF 格式的多模态模型（主权重 + mmproj 视觉投影器）
- **游戏 Profile 驱动**：每个游戏使用独立的 JSON 配置（system prompt、状态色板、UI 标签、mock 场景），在 [profiles/](profiles/) 下即可扩展
- **ImGui 实时界面**：控制面板、窗口选择、画面预览、AI 输出、日志窗口、模型下载设置
- **模型管理器**：从 HuggingFace 预设列表中一键下载主模型 + mmproj，支持 HTTP 代理
- **响应解析与容错**：对 LLM 输出做 JSON 清洗（去除 markdown 围栏等），失败时回退到安全默认值
- **Mock 推理模式**：无需加载大模型即可用 profile 中的 `mock_scenarios` 驱动 UI 开发和调试

## 目录结构

```
src/
├── main.cpp               # Windows 入口
├── main_macos.mm          # macOS 入口
├── capture/               # 屏幕捕获（WGC / ScreenCaptureKit）
├── core/                  # FrameBuffer、GameProfile、ResponseParser、LogBuffer
├── gui/                   # ImGui 界面与预览纹理（D3D11 / OpenGL）
├── inference/             # IInferenceEngine + VlmEngine（llama.cpp 封装）
├── models/                # ModelManager：HuggingFace 下载
├── platform/              # 平台相关小工具
└── test/                  # NaviVisionTest / VlmLoadTest
profiles/                  # 游戏配置 JSON（warcraft3、minesweeper…）
scripts/                   # 构建与打包脚本
third_party/nlohmann/      # JSON 库（header-only）
```

## 依赖

- CMake ≥ 3.20，支持 C++20 的编译器
- [Dear ImGui](https://github.com/ocornut/imgui)（v1.91.8，CMake 未通过 vcpkg 找到时会自动以 `FetchContent` 拉取源码构建）
- [llama.cpp](https://github.com/ggml-org/llama.cpp)：**必须**与 `navi-vision` 放在**同级目录**（`CMakeLists.txt` 使用 `${CMAKE_SOURCE_DIR}/../llama.cpp` 引用），并包含 `tools/mtmd` 子目录
- **Windows**：Windows SDK 10.0.19041.0+（WGC `CreateFreeThreaded` 所需）、D3D11
- **macOS**：部署目标 14.0，依赖 GLFW 3.3+、libcurl、Cocoa / CoreGraphics / OpenGL framework
  - 当前版本禁用了 Metal 后端（macOS 26 SDK 与 llama.cpp Metal 存在兼容性问题）

### 目录摆放

```
parent/
├── llama.cpp/        # 与本仓库同级
└── navi-vision/      # 本仓库
```

## 构建与运行

### macOS

```bash
brew install cmake glfw curl
./scripts/build_and_run_macos.sh
```

脚本会在 [build_macos/](build_macos/) 下配置并构建，随后依次运行 `NaviVisionTest` 与 `NaviVision`。

可选：通过 vcpkg 提供 imgui，设置 `VCPKG_ROOT` 后脚本会自动传入工具链参数。

### Windows

使用 Visual Studio 2022 生成器配置，例如：

```bat
cmake -B build4 -S . -G "Visual Studio 17 2022" -A x64
cmake --build build4 --config Release
scripts\build_and_run.bat
```

[scripts/build_and_run.bat](scripts/build_and_run.bat) 默认假定构建目录为 `build4/`，需按实际情况调整。

### 打包 macOS .app

```bash
./scripts/package_macos.sh                # 使用 build_macos/
./scripts/package_macos.sh build_other    # 指定其它构建目录
```

生成 [release/NaviVision.app](release/)：自动生成 `Info.plist`，将 `libllama / libggml* / libmtmd` 等运行时 dylib 嵌入 `Contents/Frameworks/`，并修正 rpath 为 `@executable_path/../Frameworks`，一并收集 Homebrew 等外部依赖。

## 构建产物

| 目标             | 说明                                                       |
| ---------------- | ---------------------------------------------------------- |
| `NaviVision`     | 主程序（Windows 为 `WIN32` GUI 程序，macOS 为 bundle 可执行） |
| `NaviVisionTest` | 控制台单测：FrameBuffer / ResponseParser / GameProfile     |
| `VlmLoadTest`    | 验证 VLM 模型能否成功加载（链接 llama / mtmd / ggml）      |

CMake 会在构建后将 [profiles/](profiles/) 目录复制到可执行文件同级目录；Windows 下还会额外复制 `llama.dll`、`ggml*.dll`、`mtmd.dll`。

## 模型下载

首次启动后，在 GUI 的「模型设置」面板中选择预设模型（参见 [src/models/model_manager.h](src/models/model_manager.h) 中的 `ModelManager::getDefaultModels`），一键下载：

- 主权重 `*.gguf`
- 视觉投影器 `mmproj*.gguf`

均保存至可执行文件同级的 `models/` 目录。若需要通过 HTTP 代理访问 HuggingFace，可在设置面板填写（如 `127.0.0.1:7890`）。

## 游戏 Profile

[profiles/](profiles/) 下的每个 JSON 文件描述一个游戏的分析策略，字段含义参见 [src/core/game_profile.h](src/core/game_profile.h)：

- `system_prompt` / `user_prompt`：送入 VLM 的提示词，要求模型只输出规范的 JSON
- `status_styles`：状态值（如 `safe` / `combat` / `playing`）对应的颜色与 UI 标签
- `display_labels`：GUI 中各字段的显示文本
- `mock_scenarios`：Mock 推理使用的预设响应

当前内置：

- [profiles/warcraft3.json](profiles/warcraft3.json) — 魔兽争霸 III
- [profiles/minesweeper.json](profiles/minesweeper.json) — Windows 扫雷

添加新游戏只需在该目录放置新的 JSON，无需改动任何 C++ 代码。

## VLM 推理关键参数

见 [src/inference/vlm_engine.h](src/inference/vlm_engine.h) 中的 `VlmConfig`：

| 字段           | 默认值 | 说明                                     |
| -------------- | ------ | ---------------------------------------- |
| `n_gpu_layers` | 99     | GPU 卸载层数（99 ≈ 全部）                |
| `n_threads`    | 4      | CPU 推理线程数                           |
| `n_ctx`        | 4096   | 上下文窗口（需容纳图像 token + 文本）    |
| `n_batch`      | 512    | 批处理大小                               |
| `temperature`  | 0.1    | 低温采样，利于产出稳定的结构化 JSON      |
| `max_tokens`   | 512    | 生成上限                                 |

## 开发备注

- NaviVision 本体使用 **C++20**；llama.cpp 以 **C++17** 编译，规避 `char8_t` 相关问题
- macOS 下 `.mm` 源文件被强制以 `-x objective-c++` 编译
- UI 线程与 VLM 推理线程通过 `AppGui` 内部的异步队列交互，`VlmEngine::analyze_frame` 在互斥锁保护下串行执行
- [src/core/response_parser.h](src/core/response_parser.h) 会剥离 ` ```json ... ``` ` 围栏并在 JSON 不合法时返回 fallback

## License

本项目的依赖许可遵循各自上游（llama.cpp、Dear ImGui、nlohmann/json 等）。项目自身尚未声明 License，使用前请自行确认。
