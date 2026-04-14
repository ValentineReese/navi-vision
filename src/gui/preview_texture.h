#pragma once

// ============================================================
//  PreviewTexture — 跨平台预览纹理抽象
//
//  封装 D3D11 (Windows) 或 OpenGL (macOS) 纹理操作，
//  为 AppGui 提供统一的预览图像上传/显示接口。
// ============================================================

#include <cstdint>
#include <imgui.h>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace navi {

class PreviewTexture {
public:
#ifdef _WIN32
    PreviewTexture(ID3D11Device* device, ID3D11DeviceContext* context);
#else
    PreviewTexture();
#endif
    ~PreviewTexture();

    // 禁止拷贝
    PreviewTexture(const PreviewTexture&) = delete;
    PreviewTexture& operator=(const PreviewTexture&) = delete;

    /// 上传 BGRA 像素数据到 GPU 纹理
    /// @param bgra_data  BGRA 像素数据（width * height * 4 字节）
    /// @param width      图像宽度
    /// @param height     图像高度
    void update(const uint8_t* bgra_data, int width, int height);

    /// 获取 ImGui 纹理 ID（用于 ImGui::Image()）
    ImTextureID getImTextureID() const;

    /// 纹理是否有效
    bool isValid() const;

    int width() const { return width_; }
    int height() const { return height_; }

    /// 释放 GPU 资源
    void reset();

private:
    int width_  = 0;
    int height_ = 0;

#ifdef _WIN32
    ID3D11Device*        device_  = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
#else
    unsigned int textureId_ = 0;
#endif
};

} // namespace navi
