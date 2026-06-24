#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

namespace cocos2d {
class CCNode;
class CCTexture2D;
}

namespace paimon::capture {

struct ScreenSize {
    float width = 0.f;
    float height = 0.f;
};

bool isActive();
ScreenSize getScreenSize();

/// Enables capture context for sprites with manual draw() (PaimonShader*).
class ActiveGuard {
public:
    explicit ActiveGuard(cocos2d::CCSize const& logicalSize);
    ~ActiveGuard();
    ActiveGuard(ActiveGuard const&) = delete;
    ActiveGuard& operator=(ActiveGuard const&) = delete;

private:
    bool m_hadPrev = false;
    bool m_prevActive = false;
    ScreenSize m_prevSize{};
};

bool playLayerShaderCaptureActive();

void renderSceneGraph(cocos2d::CCNode* scene);

bool readBoundFramebufferRGBA(std::vector<uint8_t>& pixels, int& outWidth, int& outHeight);

/// RGBA buffer, rows in Cocos2d orientation with flipY applied.
cocos2d::CCTexture2D* createTextureFromRGBA(
    uint8_t const* data, int width, int height, bool retain = true
);

} // namespace paimon::capture