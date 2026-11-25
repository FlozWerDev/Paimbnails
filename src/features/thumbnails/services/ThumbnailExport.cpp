#include "ThumbnailExport.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <algorithm>
#include <cmath>

namespace paimon::thumbviewer {
namespace {

struct ReadbackState {
    GLint previousFramebuffer = 0;
    GLint previousPackAlignment = 4;
    GLuint framebuffer = 0;

    ReadbackState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glGenFramebuffers(1, &framebuffer);
    }

    ~ReadbackState() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    }
};

}

TexturePixels readTexturePixels(cocos2d::CCTexture2D* texture) {
    if (!texture || !texture->getName()) return {};
    auto* director = cocos2d::CCDirector::get();
    if (!director || !director->getOpenGLView()) return {};

    auto const content = texture->getContentSizeInPixels();
    if (!std::isfinite(content.width) || !std::isfinite(content.height)
        || content.width < 1.f || content.height < 1.f
        || content.width > 16384.f || content.height > 16384.f) return {};
    auto const width = std::min(texture->getPixelsWide(), static_cast<unsigned>(std::lround(content.width)));
    auto const height = std::min(texture->getPixelsHigh(), static_cast<unsigned>(std::lround(content.height)));
    if (!width || !height || static_cast<uint64_t>(width) * height > 16ull * 1024 * 1024) return {};

    ReadbackState state;
    if (!state.framebuffer) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->getName(), 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return {};

    TexturePixels pixels{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
    if (glGetError() != GL_NO_ERROR) return {};

    // uploaded textures already store their source rows in PNG order. Drawing
    // them into a second render target would flip them and involve UI matrices.
    if (texture->hasPremultipliedAlpha()) {
        for (size_t offset = 0; offset < pixels.rgba.size(); offset += 4) {
            unsigned const alpha = pixels.rgba[offset + 3];
            if (alpha == 0 || alpha == 255) continue;
            for (size_t channel = 0; channel < 3; ++channel) {
                pixels.rgba[offset + channel] = static_cast<uint8_t>(std::min(255u,
                    (pixels.rgba[offset + channel] * 255u + alpha / 2u) / alpha));
            }
        }
    }
    return pixels;
}

}
