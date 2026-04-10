#include "app_gui.h"
#include <cstdio>
#include <cstring>

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
}

AppGui::~AppGui() {
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
    // 每帧只读取一次 FrameBuffer，缓存结果供所有子模块使用
    cachedFrame_ = visionActive_ ? buffer_->read() : nullptr;

    renderControlPanel();
    if (visionActive_) {
        renderPreview();
        renderAIPanel();
    }
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

    inferenceThread_ = std::thread([this, px = std::move(pixels), width, height]() {
        std::string raw = engine_->analyze_frame(px, width, height, "");
        GameStateData result = parse_ai_response(raw);

        {
            std::lock_guard<std::mutex> lock(inferenceMutex_);
            pendingAIResult_ = std::move(result);
        }
        hasNewResult_.store(true);
        inferenceRunning_.store(false);
    });
}

} // namespace navi
