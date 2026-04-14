#include "wgc_capture.h"
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

// ── IDirect3DDxgiInterfaceAccess 接口定义 ──
// 用于从 WinRT IDirect3DSurface 获取底层 DXGI/D3D11 资源
// GUID: {A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1}
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
    IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** p) = 0;
};

// ── CreateDirect3D11DeviceFromDXGIDevice ──
// 此函数将原生 DXGI 设备包装为 WinRT IDirect3DDevice
extern "C" {
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
        IDXGIDevice*    dxgiDevice,
        IInspectable**  graphicsDevice);
}

namespace navi {

// ============================================================
//  构造 / 析构
// ============================================================

WgcCapture::WgcCapture(std::shared_ptr<FrameBuffer> buffer)
    : buffer_(std::move(buffer)) {
}

WgcCapture::~WgcCapture() {
    stop();
}

// ============================================================
//  公共接口
// ============================================================

bool WgcCapture::start(NativeWindowHandle windowHandle, float targetFps) {
    if (capturing_.load())
        return false;

    targetHwnd_   = reinterpret_cast<HWND>(windowHandle);
    targetFps_    = targetFps;
    stopRequested_ = false;
    initDone_      = false;
    initSuccess_   = false;

    // 在独立线程中执行初始化 + 捕获循环
    captureThread_ = std::thread(&WgcCapture::captureLoop, this);

    // 阻塞等待捕获线程完成初始化
    {
        std::unique_lock<std::mutex> lock(initMutex_);
        initCv_.wait(lock, [this] { return initDone_.load(); });
    }

    if (!initSuccess_.load()) {
        // 初始化失败，回收线程
        if (captureThread_.joinable())
            captureThread_.join();
        return false;
    }

    return true;
}

void WgcCapture::stop() {
    stopRequested_ = true;
    if (captureThread_.joinable())
        captureThread_.join();
    capturing_ = false;
}

std::string WgcCapture::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void WgcCapture::setError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = msg;
}

// ============================================================
//  D3D11 设备初始化（捕获专用）
// ============================================================

bool WgcCapture::initD3D() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    // 创建硬件 D3D11 设备（需要 BGRA 支持，WGC 默认使用 BGRA 格式）
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                      // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        d3dDevice_.GetAddressOf(),
        nullptr,
        d3dContext_.GetAddressOf());

    if (FAILED(hr)) {
        setError("D3D11CreateDevice failed: " + std::to_string(hr));
        return false;
    }

    // 将 ID3D11Device → IDXGIDevice → WinRT IDirect3DDevice
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        setError("Failed to query IDXGIDevice: " + std::to_string(hr));
        return false;
    }

    winrt::com_ptr<IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        setError("CreateDirect3D11DeviceFromDXGIDevice failed: " + std::to_string(hr));
        return false;
    }

    winrtDevice_ = inspectable.as<
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    return true;
}

// ============================================================
//  WGC 会话初始化
// ============================================================

bool WgcCapture::initCapture(HWND hwnd) {
    try {
        // ── 通过 IGraphicsCaptureItemInterop 从 HWND 创建 CaptureItem ──
        auto interop = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
        winrt::check_hresult(interop->CreateForWindow(
            hwnd,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            winrt::put_abi(item)));

        if (!item) {
            setError("CreateForWindow returned null item");
            return false;
        }

        captureItem_ = item;
        auto size = captureItem_.Size();

        // ── 创建 FreeThreaded 帧池（无需 DispatcherQueue） ──
        // 要求 Windows 10 2004+ (build 19041+)
        framePool_ = winrt::Windows::Graphics::Capture::
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice_,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                1,    // 帧池容量（只需保留 1 帧）
                size);

        // ── 创建并启动捕获会话 ──
        captureSession_ = framePool_.CreateCaptureSession(captureItem_);

        // 禁用黄色捕获边框（Windows 10 2104+ 支持）
        try {
            captureSession_.IsBorderRequired(false);
        } catch (...) {
            // 旧版 Windows 不支持此属性，忽略即可
        }

        captureSession_.StartCapture();
        return true;

    } catch (winrt::hresult_error const& ex) {
        setError("WGC init failed: " + winrt::to_string(ex.message()));
        return false;
    }
}

// ============================================================
//  捕获主循环（运行在独立后台线程）
// ============================================================

void WgcCapture::captureLoop() {
    // 初始化 WinRT 多线程套间
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ── 初始化阶段 ──
    bool success = initD3D() && initCapture(targetHwnd_);

    // 通知主线程初始化完成
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        initSuccess_ = success;
        initDone_    = true;
    }
    initCv_.notify_one();

    if (!success) {
        winrt::uninit_apartment();
        return;
    }

    // ── 进入捕获循环 ──
    capturing_       = true;
    lastCaptureTime_ = std::chrono::steady_clock::now();

    // 根据目标帧率计算帧间隔
    auto frameInterval = std::chrono::milliseconds(
        static_cast<int>(1000.0f / targetFps_));

    while (!stopRequested_.load()) {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = now - lastCaptureTime_;

        // ── 限频机制：未到时间则短暂休眠 ──
        if (elapsed < frameInterval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // ── 尝试获取最新帧 ──
        auto frame = framePool_.TryGetNextFrame();
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        try {
            // 从 WinRT Surface 获取底层 ID3D11Texture2D
            auto surface = frame.Surface();
            auto access  = surface.as<IDirect3DDxgiInterfaceAccess>();

            Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedTexture;
            winrt::check_hresult(
                access->GetInterface(IID_PPV_ARGS(&capturedTexture)));

            D3D11_TEXTURE2D_DESC desc;
            capturedTexture->GetDesc(&desc);

            // ── 处理窗口尺寸变化 ──
            auto contentSize = frame.ContentSize();
            auto itemSize    = captureItem_.Size();
            if (contentSize.Width  != itemSize.Width ||
                contentSize.Height != itemSize.Height) {
                // 窗口大小改变，重建帧池
                framePool_.Recreate(
                    winrtDevice_,
                    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    1,
                    contentSize);
                frame.Close();
                continue;
            }

            // ── 复用暂存纹理（仅尺寸变化时重建） ──
            int width  = static_cast<int>(desc.Width);
            int height = static_cast<int>(desc.Height);

            if (width != stagingWidth_ || height != stagingHeight_) {
                stagingTexture_.Reset();
                D3D11_TEXTURE2D_DESC stagingDesc = desc;
                stagingDesc.Usage          = D3D11_USAGE_STAGING;
                stagingDesc.BindFlags      = 0;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stagingDesc.MiscFlags      = 0;

                HRESULT hr = d3dDevice_->CreateTexture2D(
                    &stagingDesc, nullptr, stagingTexture_.GetAddressOf());
                if (FAILED(hr)) {
                    frame.Close();
                    continue;
                }
                stagingWidth_  = width;
                stagingHeight_ = height;
            }

            // GPU 端拷贝：捕获纹理 → 暂存纹理
            d3dContext_->CopyResource(stagingTexture_.Get(), capturedTexture.Get());

            // 映射暂存纹理到 CPU 可读内存
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = d3dContext_->Map(
                stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                frame.Close();
                continue;
            }

            auto frameData      = std::make_shared<FrameData>();
            frameData->width    = width;
            frameData->height   = height;
            frameData->channels = 4;
            frameData->pixels.resize(
                static_cast<size_t>(width) * height * 4);
            frameData->timestamp = std::chrono::steady_clock::now();

            const uint8_t* srcData = static_cast<const uint8_t*>(mapped.pData);
            uint8_t*       dstData = frameData->pixels.data();

            const size_t rowBytes = static_cast<size_t>(width) * 4;
            for (int y = 0; y < height; y++) {
                std::memcpy(dstData + y * rowBytes,
                            srcData + y * mapped.RowPitch,
                            rowBytes);
            }

            d3dContext_->Unmap(stagingTexture_.Get(), 0);
            frame.Close();

            // 将帧数据写入线程安全缓冲区
            buffer_->write(std::move(frameData));
            lastCaptureTime_ = now;

        } catch (winrt::hresult_error const&) {
            frame.Close();
            // 捕获异常时跳过该帧，继续尝试
        }
    }

    // ── 退出循环，清理资源 ──
    capturing_ = false;
    cleanup();
    winrt::uninit_apartment();
}

// ============================================================
//  资源清理
// ============================================================

void WgcCapture::cleanup() {
    if (captureSession_) {
        captureSession_.Close();
        captureSession_ = nullptr;
    }
    if (framePool_) {
        framePool_.Close();
        framePool_ = nullptr;
    }
    captureItem_ = nullptr;
    winrtDevice_ = nullptr;
    stagingTexture_.Reset();
    stagingWidth_  = 0;
    stagingHeight_ = 0;
    d3dContext_.Reset();
    d3dDevice_.Reset();
}

} // namespace navi
