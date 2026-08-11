#include "SceneCapture.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/loader/Log.hpp>
#include <algorithm>
#include <cstring>

using namespace geode::prelude;

namespace paimon::capture {
namespace {

struct State {
    bool active = false;
    ScreenSize size{};
};

State& state() {
    static State s;
    return s;
}

bool pixelBufferHasContent(std::vector<uint8_t> const& pixels) {
    if (pixels.size() < 4) return false;
    for (uint8_t b : pixels) {
        if (b != 0) return true;
    }
    return false;
}

void flipVertical(std::vector<uint8_t>& pixels, int width, int height) {
    int rowSize = width * 4;
    std::vector<uint8_t> temp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = pixels.data() + y * rowSize;
        uint8_t* bottom = pixels.data() + (height - 1 - y) * rowSize;
        std::memcpy(temp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, temp.data(), rowSize);
    }
}

} // namespace

bool isActive() { return state().active; }

ScreenSize getScreenSize() { return state().size; }

ActiveGuard::ActiveGuard(CCSize const& logicalSize) {
    auto& s = state();
    m_hadPrev = true;
    m_prevActive = s.active;
    m_prevSize = s.size;
    s.active = true;
    s.size.width = logicalSize.width;
    s.size.height = logicalSize.height;
}

ActiveGuard::~ActiveGuard() {
    if (!m_hadPrev) return;
    auto& s = state();
    s.active = m_prevActive;
    s.size = m_prevSize;
}

bool playLayerShaderCaptureActive() {
    auto* playLayer = PlayLayer::get();
    if (!playLayer || !playLayer->m_shaderLayer) return false;
    return playLayer->m_shaderLayer->m_renderTexture != nullptr;
}

void renderSceneGraph(CCNode* scene) {
    if (!scene) return;

    if (playLayerShaderCaptureActive()) {
        auto* playLayer = PlayLayer::get();
        if (playLayer && playLayer->m_shaderLayer) {
            geode::log::debug("[SceneCapture] render via ShaderLayer::visit");
            playLayer->m_shaderLayer->visit();
            return;
        }
    }

    scene->visit();

    // Visit OverlayManager (notification node) too — Geode mod content lives outside scene graph.
    if (auto* director = CCDirector::sharedDirector()) {
        if (auto* notif = director->getNotificationNode()) {
            if (notif->isVisible()) {
                notif->visit();
            }
        }
    }
}

bool readBoundFramebufferRGBA(std::vector<uint8_t>& pixels, int& outWidth, int& outHeight) {
    auto* director = CCDirector::get();
    if (!director) return false;

    auto* glView = director->getOpenGLView();
    CCSize frameSize = glView ? glView->getFrameSize() : CCSizeZero;
    CCSize winPixels = director->getWinSizeInPixels();

    GLint currentViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, currentViewport);

    GLint currentFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

    int glWidth = static_cast<int>(frameSize.width);
    int glHeight = static_cast<int>(frameSize.height);
    if (glWidth <= 0 || glHeight <= 0) {
        glWidth = static_cast<int>(winPixels.width);
        glHeight = static_cast<int>(winPixels.height);
    }
    if (glWidth <= 0 || glHeight <= 0) {
        glWidth = currentViewport[2];
        glHeight = currentViewport[3];
    }
    if (glWidth <= 0 || glHeight <= 0) return false;

    constexpr size_t kMaxPixels = SIZE_MAX / 4;
    if (static_cast<size_t>(glWidth) * static_cast<size_t>(glHeight) > kMaxPixels) {
        return false;
    }

    glFinish();
    pixels.assign(static_cast<size_t>(glWidth) * glHeight * 4, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    bool hasContent = glGetError() == GL_NO_ERROR && pixelBufferHasContent(pixels);

    if (!hasContent && currentFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFinish();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        hasContent = glGetError() == GL_NO_ERROR && pixelBufferHasContent(pixels);
        glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);
    }

    if (!hasContent && (currentViewport[2] != glWidth || currentViewport[3] != glHeight)
        && currentViewport[2] > 0 && currentViewport[3] > 0) {
        int vpW = currentViewport[2];
        int vpH = currentViewport[3];
        int vpX = currentViewport[0];
        int vpY = currentViewport[1];

        std::vector<uint8_t> vpPixels(static_cast<size_t>(vpW) * vpH * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFinish();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(vpX, vpY, vpW, vpH, GL_RGBA, GL_UNSIGNED_BYTE, vpPixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

        if (glGetError() == GL_NO_ERROR && pixelBufferHasContent(vpPixels)) {
            pixels = std::move(vpPixels);
            glWidth = vpW;
            glHeight = vpH;
            hasContent = true;
        }
    }

    if (!hasContent) return false;

    flipVertical(pixels, glWidth, glHeight);
    outWidth = glWidth;
    outHeight = glHeight;
    return true;
}

CCTexture2D* createTextureFromRGBA(uint8_t const* data, int width, int height, bool retain) {
    if (!data || width <= 0 || height <= 0) return nullptr;

    auto* tex = new CCTexture2D();
    if (!tex->initWithData(
            data,
            kCCTexture2DPixelFormat_RGBA8888,
            width,
            height,
            CCSize(static_cast<float>(width), static_cast<float>(height)))) {
        tex->release();
        return nullptr;
    }
    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);
    tex->autorelease();
    if (retain) tex->retain();
    return tex;
}

} // namespace paimon::capture