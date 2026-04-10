#include "app_gui.h"
#include "../inference/vlm_engine.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace navi {

// ============================================================
//  构造 / 析构
// ============================================================

AppGui::AppGui(std::shared_ptr<WgcCapture>       capture,
               std::shared_ptr<FrameBuffer>    buffer,
               std::shared_ptr<IInferenceEngine> engine,
               ID3D11Device*                  device,
               ID3D11DeviceContext*           context)
    : capture_(std::move(capture))
    , buffer_(std::move(buffer))
    , engine_(std::move(engine))
    , device_(device)
    , context_(context) {
    defaultModels_ = ModelManager::getDefaultModels();
    modelsDir_ = ModelManager::getModelsDir();
    if (!defaultModels_.empty()) {
        snprintf(customModelUrl_, sizeof(customModelUrl_), "%s",
                 defaultModels_[0].model_url.c_str());
        snprintf(customMmprojUrl_, sizeof(customMmprojUrl_), "%s",
                 defaultModels_[0].mmproj_url.c_str());
    }
}

AppGui::~AppGui() {
    cancelDownload_.store(true);
    if (downloadThread_.joinable())
        downloadThread_.join();
    if (modelLoadThread_.joinable())
        modelLoadThread_.join();
    // 等待推理线程结束
    if (inferenceThread_.joinable())
        inferenceThread_.join();
    previewSRV_.Reset();
    previewTexture_.Reset();
}

// ============================================================
//  主渲染入口
// ============================================================

void AppGui::render() {
    // Handle pending model swap from background load thread
    if (modelLoadDone_.load()) {
        {
            std::lock_guard<std::mutex> lock(modelLoadMutex_);
            if (pendingEngine_) {
                engine_ = std::move(pendingEngine_);
            }
        }
        modelLoadDone_.store(false);
        modelLoading_.store(false);
    }

    // 每帧只读取一次 FrameBuffer，缓存结果供所有子模块使用
    cachedFrame_ = visionActive_ ? buffer_->read() : nullptr;

    renderControlPanel();
    if (visionActive_) {
        renderPreview();
        renderAIPanel();
    }
    renderModelSettings();
}

// ============================================================
//  控制面板
// ============================================================

void AppGui::renderControlPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("NaviVision Control",
                 nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("NaviVision");
    ImGui::SameLine();
    ImGui::TextDisabled("v0.1");
    ImGui::Separator();

    // ── "眼睛"按钮 ──
    ImVec2 btnSize(160, 45);

    if (visionActive_) {
        // 绿色 —— 正在捕获
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.65f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.20f, 0.75f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.10f, 0.55f, 0.10f, 1.0f));
        if (ImGui::Button("(O) Vision ON", btnSize)) {
            capture_->stop();
            visionActive_ = false;
            buffer_->clear();
        }
        ImGui::PopStyleColor(3);
    } else {
        // 灰色 —— 未捕获
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.45f, 0.45f, 0.48f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
        if (ImGui::Button("(-) Vision OFF", btnSize)) {
            // 点击后弹出窗口选择列表
            windowList_        = WindowEnumerator::enumerate();
            showWindowSelector_ = true;
            ImGui::OpenPopup("SelectWindow");
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();

    // ── "话筒"按钮（占位） ──
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.45f, 0.45f, 0.48f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
    ImGui::Button("[x] Mic OFF", btnSize);
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Voice module - coming soon");
    }

    // ── 状态栏 ──
    renderStatusBar();

    // ── 模型设置入口 ──
    ImGui::Separator();
    if (engine_) {
        ImGui::TextDisabled("Engine: %s", engine_->engine_name().c_str());
    }
    if (modelLoading_.load()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Loading model...");
    }
    if (ImGui::Button("Model Settings", ImVec2(-1, 28))) {
        showModelSettings_ = !showModelSettings_;
    }

    // 窗口选择弹窗必须在同一个 ImGui 窗口的 ID 栈内渲染
    renderWindowSelector();

    ImGui::End();
}

// ============================================================
//  窗口选择弹窗
// ============================================================

void AppGui::renderWindowSelector() {
    if (!showWindowSelector_)
        return;

    // 居中弹窗
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("SelectWindow", &showWindowSelector_,
                                ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Select a window to capture:");
        ImGui::Separator();

        // 刷新按钮
        if (ImGui::Button("Refresh")) {
            windowList_ = WindowEnumerator::enumerate();
        }
        ImGui::Separator();

        // 窗口列表（可滚动）
        ImGui::BeginChild("WindowList", ImVec2(0, -30), true);
        for (size_t i = 0; i < windowList_.size(); i++) {
            std::string label = wstringToUtf8(windowList_[i].title);
            // 添加序号防止 ImGui ID 冲突
            char id[512];
            snprintf(id, sizeof(id), "%s##%zu", label.c_str(), i);

            if (ImGui::Selectable(id)) {
                // 用户选中了目标窗口，启动捕获
                if (capture_->start(windowList_[i].hwnd, 3.0f)) {
                    visionActive_ = true;
                }
                showWindowSelector_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showWindowSelector_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ============================================================
//  画面预览
// ============================================================

void AppGui::renderPreview() {
    updatePreviewTexture();

    if (!previewSRV_)
        return;

    ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Preview", nullptr, ImGuiWindowFlags_NoCollapse);

    // 计算保持宽高比的预览尺寸
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float aspect = (previewHeight_ > 0)
                       ? static_cast<float>(previewWidth_) / previewHeight_
                       : 16.0f / 9.0f;

    float dispW = avail.x;
    float dispH = dispW / aspect;
    if (dispH > avail.y) {
        dispH = avail.y;
        dispW = dispH * aspect;
    }

    ImGui::Image(static_cast<ImTextureID>(previewSRV_.Get()),
                 ImVec2(dispW, dispH));

    ImGui::End();
}

void AppGui::updatePreviewTexture() {
    auto frame = cachedFrame_;
    if (!frame)
        return;

    // 如果时间戳未变化，说明帧没有更新，跳过
    if (frame->timestamp == lastPreviewTimestamp_)
        return;
    lastPreviewTimestamp_ = frame->timestamp;

    // 如果帧尺寸变化，需重建纹理
    if (frame->width != previewWidth_ || frame->height != previewHeight_) {
        previewSRV_.Reset();
        previewTexture_.Reset();

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width            = frame->width;
        texDesc.Height           = frame->height;
        texDesc.MipLevels        = 1;
        texDesc.ArraySize        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage            = D3D11_USAGE_DYNAMIC;
        texDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device_->CreateTexture2D(
            &texDesc, nullptr, previewTexture_.GetAddressOf());
        if (FAILED(hr)) return;

        hr = device_->CreateShaderResourceView(
            previewTexture_.Get(), nullptr, previewSRV_.GetAddressOf());
        if (FAILED(hr)) return;

        previewWidth_  = frame->width;
        previewHeight_ = frame->height;
    }

    // 将 BGRA 像素数据直接上传到 BGRA 纹理（零转换，仅 memcpy）
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(
        previewTexture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    const uint8_t* src = frame->pixels.data();
    uint8_t*       dst = static_cast<uint8_t*>(mapped.pData);

    const size_t rowBytes = static_cast<size_t>(frame->width) * 4;
    for (int y = 0; y < frame->height; y++) {
        std::memcpy(dst + y * mapped.RowPitch,
                    src + y * rowBytes,
                    rowBytes);
    }

    context_->Unmap(previewTexture_.Get(), 0);
}

// ============================================================
//  AI Analysis Panel
//
//  Periodically invokes the inference engine on the latest frame
//  and renders the structured AI response in a dedicated window.
// ============================================================

void AppGui::renderAIPanel() {
    // Pick up any result from background inference thread
    if (hasNewResult_.load()) {
        std::lock_guard<std::mutex> lock(inferenceMutex_);
        lastAIResult_ = pendingAIResult_;
        hasNewResult_.store(false);
    }

    // Throttle: only launch inference every analysisIntervalSec_ seconds
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - lastAnalysisTime_).count();

    if (engine_ && elapsed >= analysisIntervalSec_ && !inferenceRunning_.load()) {
        auto frame = cachedFrame_;
        if (frame && !frame->pixels.empty()) {
            // Launch inference on a background thread
            lastAnalysisTime_ = now;
            // Copy pixel data to avoid keeping shared_ptr alive in thread
            runInferenceAsync(frame->pixels, frame->width, frame->height);
        }
    }

    // Render the AI result panel
    ImGui::SetNextWindowPos(ImVec2(440, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("AI Analysis", nullptr, ImGuiWindowFlags_NoCollapse);

    if (engine_) {
        ImGui::TextDisabled("Engine: %s", engine_->engine_name().c_str());
    }
    ImGui::Separator();

    // Fallback indicator
    if (lastAIResult_.is_fallback) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "[Fallback] No valid AI response yet");
    }

    // Status with color coding
    ImVec4 statusColor(0.6f, 0.6f, 0.6f, 1.0f);
    if (lastAIResult_.current_status == "safe")
        statusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
    else if (lastAIResult_.current_status == "combat")
        statusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    else if (lastAIResult_.current_status == "retreating")
        statusColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);

    ImGui::TextColored(statusColor, "Status: %s",
                       lastAIResult_.current_status.c_str());

    // Confidence bar
    ImGui::Text("Confidence:");
    ImGui::SameLine();
    ImGui::ProgressBar(lastAIResult_.confidence, ImVec2(150, 16));

    ImGui::Separator();

    // Detected units
    ImGui::Text("Detected Units:");
    if (lastAIResult_.detected_units.empty()) {
        ImGui::TextDisabled("  (none)");
    } else {
        for (const auto& unit : lastAIResult_.detected_units) {
            ImGui::BulletText("%s", unit.c_str());
        }
    }

    ImGui::Separator();

    // Tactical advice
    ImGui::Text("Tactical Advice:");
    ImGui::TextWrapped("%s", lastAIResult_.tactical_advice.c_str());

    ImGui::End();
}

// ============================================================
//  状态栏
// ============================================================

void AppGui::renderStatusBar() {
    ImGui::Separator();

    if (visionActive_ && capture_->isCapturing()) {
        auto frame = cachedFrame_;
        if (frame) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Capturing");
            ImGui::SameLine();
            ImGui::Text("| %dx%d", frame->width, frame->height);
            ImGui::SameLine();
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - frame->timestamp);
            ImGui::Text("| Frame age: %lld ms", static_cast<long long>(age.count()));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                               "Capturing (waiting for first frame...)");
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Idle");
        // 显示上次错误（如果有）
        std::string err = capture_->lastError();
        if (!err.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "Error: %s", err.c_str());
        }
    }
}

// ============================================================
//  工具函数
// ============================================================

std::string AppGui::wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

// ============================================================
//  异步推理
// ============================================================

void AppGui::runInferenceAsync(std::vector<uint8_t> pixels, int width, int height) {
    // Join any previous thread that has finished
    if (inferenceThread_.joinable())
        inferenceThread_.join();

    inferenceRunning_.store(true);

    inferenceThread_ = std::thread([this, eng = engine_, px = std::move(pixels), width, height]() {
        std::string raw = eng->analyze_frame(px, width, height, "");
        GameStateData result = parse_ai_response(raw);

        {
            std::lock_guard<std::mutex> lock(inferenceMutex_);
            pendingAIResult_ = std::move(result);
        }
        hasNewResult_.store(true);
        inferenceRunning_.store(false);
    });
}

// ============================================================
//  模型设置窗口
// ============================================================

void AppGui::renderModelSettings() {
    if (!showModelSettings_) return;

    ImGui::SetNextWindowPos(ImVec2(200, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Model Settings", &showModelSettings_)) {
        ImGui::End();
        return;
    }

    // ── 预设模型选择 ──
    ImGui::Text("Preset Models (HuggingFace):");
    if (ImGui::BeginCombo("##PresetCombo",
                          defaultModels_[selectedModelIdx_].name.c_str()))
    {
        for (int i = 0; i < static_cast<int>(defaultModels_.size()); i++) {
            bool selected = (i == selectedModelIdx_);
            if (ImGui::Selectable(defaultModels_[i].name.c_str(), selected)) {
                selectedModelIdx_ = i;
                snprintf(customModelUrl_, sizeof(customModelUrl_), "%s",
                         defaultModels_[i].model_url.c_str());
                snprintf(customMmprojUrl_, sizeof(customMmprojUrl_), "%s",
                         defaultModels_[i].mmproj_url.c_str());
            }
            if (selected) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", defaultModels_[i].description.c_str());
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("%s", defaultModels_[selectedModelIdx_].description.c_str());

    // 根据当前 URL 检查本地文件是否存在
    std::string modelFname  = ModelManager::filenameFromUrl(customModelUrl_);
    std::string mmprojFname = ModelManager::filenameFromUrl(customMmprojUrl_);
    auto localModel  = std::filesystem::path(modelsDir_) / modelFname;
    auto localMmproj = std::filesystem::path(modelsDir_) / mmprojFname;
    bool filesExist  = std::filesystem::exists(localModel) &&
                       std::filesystem::exists(localMmproj);

    if (filesExist) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: Downloaded");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Status: Not Downloaded");
    }

    ImGui::Separator();

    // ── URL 输入（可编辑，支持自定义 URL） ──
    ImGui::Text("Model URL:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ModelURL", customModelUrl_, sizeof(customModelUrl_));

    ImGui::Text("Vision Projector URL:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##MmprojURL", customMmprojUrl_, sizeof(customMmprojUrl_));

    ImGui::TextDisabled("Target: %s", modelsDir_.c_str());

    ImGui::Separator();

    // ── 下载区域 ──
    if (downloading_.load()) {
        float progress = downloadProgress_.load();
        ImGui::ProgressBar(progress, ImVec2(-1, 0));

        size_t downloaded = dlBytesDown_.load();
        size_t total      = dlBytesTotal_.load();
        std::lock_guard<std::mutex> lock(downloadMsgMutex_);
        if (total > 0) {
            ImGui::Text("%s  (%s / %s)",
                        downloadStatus_.c_str(),
                        ModelManager::formatBytes(downloaded).c_str(),
                        ModelManager::formatBytes(total).c_str());
        } else if (downloaded > 0) {
            ImGui::Text("%s  (%s)",
                        downloadStatus_.c_str(),
                        ModelManager::formatBytes(downloaded).c_str());
        } else {
            ImGui::Text("%s", downloadStatus_.c_str());
        }

        if (ImGui::Button("Cancel", ImVec2(-1, 28))) {
            cancelDownload_.store(true);
        }
    } else {
        bool urlsValid = customModelUrl_[0] != '\0' && customMmprojUrl_[0] != '\0';
        ImGui::BeginDisabled(!urlsValid || modelLoading_.load());
        if (ImGui::Button("Download", ImVec2(-1, 28))) {
            startModelDownload();
        }
        ImGui::EndDisabled();

        // 显示上次下载状态（失败信息用红色）
        {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            if (!downloadStatus_.empty()) {
                bool isError = downloadStatus_.find("failed") != std::string::npos
                            || downloadStatus_.find("error")  != std::string::npos
                            || downloadStatus_.find("HTTP")   != std::string::npos;
                if (isError) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
                }
                ImGui::TextWrapped("%s", downloadStatus_.c_str());
                if (isError) {
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    ImGui::Separator();

    // ── 加载 / 卸载 ──
    if (modelLoading_.load()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Loading model...");
    } else {
        float halfWidth = ImGui::GetContentRegionAvail().x * 0.5f - 4;

        ImGui::BeginDisabled(!filesExist || downloading_.load());
        if (ImGui::Button("Load Model", ImVec2(halfWidth, 30))) {
            startModelLoad();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        bool isVlm = engine_ && engine_->engine_name() != "MockInference";
        ImGui::BeginDisabled(!isVlm || inferenceRunning_.load());
        if (ImGui::Button("Unload", ImVec2(-1, 30))) {
            engine_ = std::make_shared<MockInference>();
        }
        ImGui::EndDisabled();
    }

    // 加载错误信息
    {
        std::lock_guard<std::mutex> lock(modelLoadMutex_);
        if (!modelLoadError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "%s", modelLoadError_.c_str());
        }
    }

    // 当前引擎信息
    if (engine_) {
        ImGui::TextDisabled("Current: %s", engine_->engine_name().c_str());
    }

    ImGui::End();
}

// ============================================================
//  后台下载（model + mmproj 两阶段）
// ============================================================

void AppGui::startModelDownload() {
    if (downloading_.load()) return;

    std::string modelUrl  = customModelUrl_;
    std::string mmprojUrl = customMmprojUrl_;
    if (modelUrl.empty() || mmprojUrl.empty()) return;

    std::string modelDest  = (std::filesystem::path(modelsDir_) /
                              ModelManager::filenameFromUrl(modelUrl)).string();
    std::string mmprojDest = (std::filesystem::path(modelsDir_) /
                              ModelManager::filenameFromUrl(mmprojUrl)).string();

    downloading_.store(true);
    cancelDownload_.store(false);
    downloadProgress_.store(0.0f);
    dlBytesDown_.store(0);
    dlBytesTotal_.store(0);
    {
        std::lock_guard<std::mutex> lock(downloadMsgMutex_);
        downloadStatus_.clear();
    }

    if (downloadThread_.joinable()) downloadThread_.join();

    downloadThread_ = std::thread(
        [this, modelUrl, mmprojUrl, modelDest, mmprojDest]()
    {
        std::string dlError;

        // Phase 1: 下载主模型
        {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            downloadStatus_ = "Downloading model...";
        }

        bool ok = ModelManager::downloadFile(modelUrl, modelDest,
            [this](size_t downloaded, size_t total) {
                dlBytesDown_.store(downloaded);
                dlBytesTotal_.store(total);
                if (total > 0)
                    downloadProgress_.store(
                        static_cast<float>(downloaded) / total * 0.5f);
            }, cancelDownload_, dlError);

        if (!ok) {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            if (cancelDownload_.load()) {
                downloadStatus_ = "Download cancelled";
            } else {
                downloadStatus_ = "Model download failed:\n" + dlError
                                + "\nURL: " + modelUrl;
            }
            downloading_.store(false);
            return;
        }

        // Phase 2: 下载视觉投影器
        {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            downloadStatus_ = "Downloading vision projector...";
        }
        dlBytesDown_.store(0);
        dlBytesTotal_.store(0);

        ok = ModelManager::downloadFile(mmprojUrl, mmprojDest,
            [this](size_t downloaded, size_t total) {
                dlBytesDown_.store(downloaded);
                dlBytesTotal_.store(total);
                if (total > 0)
                    downloadProgress_.store(
                        0.5f + static_cast<float>(downloaded) / total * 0.5f);
            }, cancelDownload_, dlError);

        if (!ok) {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            if (cancelDownload_.load()) {
                downloadStatus_ = "Download cancelled";
            } else {
                downloadStatus_ = "Vision projector download failed:\n" + dlError
                                + "\nURL: " + mmprojUrl;
            }
            downloading_.store(false);
            return;
        }

        downloadProgress_.store(1.0f);
        {
            std::lock_guard<std::mutex> lock(downloadMsgMutex_);
            downloadStatus_ = "Download complete!";
        }
        downloading_.store(false);
    });
}

// ============================================================
//  后台加载模型（创建 VlmEngine）
// ============================================================

void AppGui::startModelLoad() {
    if (modelLoading_.load() || downloading_.load()) return;

    std::string modelPath  = (std::filesystem::path(modelsDir_) /
                              ModelManager::filenameFromUrl(customModelUrl_)).string();
    std::string mmprojPath = (std::filesystem::path(modelsDir_) /
                              ModelManager::filenameFromUrl(customMmprojUrl_)).string();

    if (!std::filesystem::exists(modelPath) ||
        !std::filesystem::exists(mmprojPath)) return;

    modelLoading_.store(true);
    {
        std::lock_guard<std::mutex> lock(modelLoadMutex_);
        modelLoadError_.clear();
    }

    if (modelLoadThread_.joinable()) modelLoadThread_.join();

    modelLoadThread_ = std::thread([this, modelPath, mmprojPath]() {
        VlmConfig cfg;
        cfg.model_path   = modelPath;
        cfg.mmproj_path  = mmprojPath;
        cfg.n_gpu_layers = 99;
        cfg.n_threads    = 4;
        cfg.n_ctx        = 4096;
        cfg.temperature  = 0.1f;

        auto vlm = std::make_shared<VlmEngine>(cfg);

        std::lock_guard<std::mutex> lock(modelLoadMutex_);
        if (vlm->is_loaded()) {
            pendingEngine_ = vlm;
            modelLoadError_.clear();
        } else {
            pendingEngine_ = nullptr;
            modelLoadError_ = "Load failed: " + vlm->last_error();
        }
        modelLoadDone_.store(true);
    });
}

} // namespace navi
