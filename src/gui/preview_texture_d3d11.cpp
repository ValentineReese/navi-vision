#include "preview_texture.h"
#include <cstring>

namespace navi {

PreviewTexture::PreviewTexture(ID3D11Device* device, ID3D11DeviceContext* context)
    : device_(device), context_(context) {
}

PreviewTexture::~PreviewTexture() {
    reset();
}

void PreviewTexture::update(const uint8_t* bgra_data, int width, int height) {
    if (!device_ || !context_ || !bgra_data || width <= 0 || height <= 0)
        return;

    // 尺寸变化时重建纹理
    if (width != width_ || height != height_) {
        srv_.Reset();
        texture_.Reset();

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.MipLevels        = 1;
        texDesc.ArraySize        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage            = D3D11_USAGE_DYNAMIC;
        texDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr)) return;

        hr = device_->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf());
        if (FAILED(hr)) return;

        width_  = width;
        height_ = height;
    }

    // 上传像素数据到纹理
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; y++) {
        std::memcpy(
            static_cast<uint8_t*>(mapped.pData) + y * mapped.RowPitch,
            bgra_data + y * rowBytes,
            rowBytes);
    }

    context_->Unmap(texture_.Get(), 0);
}

ImTextureID PreviewTexture::getImTextureID() const {
    return static_cast<ImTextureID>(srv_.Get());
}

bool PreviewTexture::isValid() const {
    return srv_.Get() != nullptr;
}

void PreviewTexture::reset() {
    srv_.Reset();
    texture_.Reset();
    width_  = 0;
    height_ = 0;
}

} // namespace navi
