#include "preview_texture.h"
#include <cstring>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace navi {

PreviewTexture::PreviewTexture() {
}

PreviewTexture::~PreviewTexture() {
    reset();
}

void PreviewTexture::update(const uint8_t* bgra_data, int width, int height) {
    if (!bgra_data || width <= 0 || height <= 0)
        return;

    // 尺寸变化时重建纹理
    if (width != width_ || height != height_) {
        reset();

        glGenTextures(1, &textureId_);
        glBindTexture(GL_TEXTURE_2D, textureId_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     width, height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, bgra_data);

        width_  = width;
        height_ = height;
    } else {
        // 尺寸未变，仅更新像素数据
        glBindTexture(GL_TEXTURE_2D, textureId_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        width, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, bgra_data);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

ImTextureID PreviewTexture::getImTextureID() const {
    return static_cast<ImTextureID>(textureId_);
}

bool PreviewTexture::isValid() const {
    return textureId_ != 0;
}

void PreviewTexture::reset() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
    width_  = 0;
    height_ = 0;
}

} // namespace navi
