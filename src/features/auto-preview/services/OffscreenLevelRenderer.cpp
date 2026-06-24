#include "OffscreenLevelRenderer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "PreviewZonePicker.hpp"
#include "AutoPreviewGenerator.hpp"   // downscaleRGBA
#include "../AutoPreviewConfig.hpp"
#include "../../../utils/RenderTexture.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

namespace {

constexpr float kDesignH = 320.f;          // GD camera height in world units (zoom 1)
constexpr int   kSupersample = 2;          // render at Nx then downscale for AA/sharpness

bool bufferHasContent(std::vector<uint8_t> const& px) {
    if (px.size() < 16) return false;
    size_t nonBlack = 0;
    size_t const pixels = px.size() / 4;
    for (size_t i = 0; i < pixels; ++i) {
        if (px[i * 4 + 0] > 8 || px[i * 4 + 1] > 8 || px[i * 4 + 2] > 8) ++nonBlack;
    }
    return nonBlack > pixels / 40; // >2.5% non-black
}

void flipVertical(std::vector<uint8_t>& px, int w, int h) {
    int rowSize = w * 4;
    std::vector<uint8_t> tmp(rowSize);
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* top = px.data() + static_cast<size_t>(y) * rowSize;
        uint8_t* bot = px.data() + static_cast<size_t>(h - 1 - y) * rowSize;
        std::memcpy(tmp.data(), top, rowSize);
        std::memcpy(top, bot, rowSize);
        std::memcpy(bot, tmp.data(), rowSize);
    }
}

float measureLevelLength(PlayLayer* pl) {
    float maxX = 0.f;
    if (!pl->m_objects) return 0.f;
    int const count = pl->m_objects->count();
    for (int i = 0; i < count; ++i) {
        auto* obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
        if (!obj) continue;
        float x = obj->getPositionX();
        if (x > maxX) maxX = x;
    }
    return maxX;
}
// is what the FBO shows, and force only the objects inside that window visible
// (bypasses GD's camera-progress culling which keeps mid-level objects hidden
// until the player reaches them). Returns the number of objects shown.
int setupWindow(PlayLayer* pl, float zoneX, float designW) {
    float const camLeftX = zoneX - designW * 0.5f;
    float const marginX = 90.f;
    float const winMinX = camLeftX - marginX;
    float const winMaxX = camLeftX + designW + marginX;
    double ySum = 0.0;
    int yCount = 0;
    int shown = 0;
    int const count = pl->m_objects ? pl->m_objects->count() : 0;
    for (int i = 0; i < count; ++i) {
        auto* obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
        if (!obj) continue;
        float ox = obj->getPositionX();
        bool inWindow = ox >= winMinX && ox <= winMaxX;
        obj->setVisible(inWindow);
        if (inWindow) {
            ySum += obj->getPositionY();
            ++yCount;
            ++shown;
        }
    }

    float camBottomY = 0.f;
    if (yCount > 0) {
        float avgY = static_cast<float>(ySum / yCount);
        camBottomY = std::max(0.f, avgY - kDesignH * 0.5f);
    }

    pl->m_gameState.m_cameraPosition.x = camLeftX;
    pl->m_gameState.m_cameraPosition.y = camBottomY;
    pl->m_gameState.m_cameraPosition2.x = camLeftX;
    pl->m_gameState.m_cameraPosition2.y = camBottomY;
    if (pl->m_objectLayer) {
        pl->m_objectLayer->setPosition(-camLeftX, -camBottomY);
    }
    return shown;
}

bool renderWindow(CCNode* scene, int rw, int rh, std::vector<uint8_t>& out) {
    RenderTexture rt(static_cast<uint32_t>(rw), static_cast<uint32_t>(rh));
    rt.begin();
    scene->visit();
    rt.end();
    auto data = rt.getData();
    if (!data) return false;
    size_t bytes = static_cast<size_t>(rw) * rh * 4;
    out.assign(data.get(), data.get() + bytes);
    flipVertical(out, rw, rh);
    return true;
}

}

OffscreenRenderResult OffscreenLevelRenderer::render(GJGameLevel* level, int width, int height) {
OffscreenRenderResult result;
    if (!level || width <= 0 || height <= 0) return result;
    if (paimon::isRuntimeShuttingDown()) return result;

    auto* dir = CCDirector::get();
    if (!dir || !dir->getOpenGLView()) return result;
    if (PlayLayer::get() != nullptr) return result;
    if (level->m_levelString.empty()) return result;

    auto* gm = GameManager::get();
    PlayLayer* prevPlayLayer = gm ? gm->m_playLayer : nullptr;

    PlayLayer* pl = PlayLayer::create(level, false, false);
    if (!pl) {
        if (gm) gm->m_playLayer = prevPlayLayer;
        return result;
    }

    Ref<CCScene> scene = CCScene::create();
    if (!scene) {
        if (gm) gm->m_playLayer = prevPlayLayer;
        return result;
    }
    scene->addChild(pl);
    pl->updateLevelColors();

    bool const hasObjects = pl->m_objects && pl->m_objects->count() > 0;

    if (hasObjects) {
        float const aspect = static_cast<float>(width) / static_cast<float>(height);
        float const designW = kDesignH * aspect;
        int const rw = width * kSupersample;
        int const rh = height * kSupersample;
        float const levelLen = measureLevelLength(pl);
        float const frac = zoneFraction(level->m_levelID.value());

        std::vector<uint8_t> buf;
        bool got = false;
        if (levelLen > designW * 1.5f) {
            float const zoneX = levelLen * frac;
            int const shown = setupWindow(pl, zoneX, designW);
            if (shown > 0 && renderWindow(scene, rw, rh, buf) && bufferHasContent(buf)) {
                got = true;
            }
        }

        // Attempt 2 (fallback): start of the level.
        if (!got) {
            setupWindow(pl, designW * 0.5f, designW);
            buf.clear();
            if (renderWindow(scene, rw, rh, buf) && bufferHasContent(buf)) {
                got = true;
            }
        }

        if (got) {
        auto down = downscaleRGBA(buf.data(), rw, rh, width, height);
            if (down) {
                size_t bytes = static_cast<size_t>(width) * height * 4;
                result.rgba.assign(down.get(), down.get() + bytes);
                result.width = width;
                result.height = height;
                result.success = true;
            }
        }
    }

    scene->removeChild(pl, true);
    if (gm) gm->m_playLayer = prevPlayLayer;
    return result;
}

} // namespace paimon::autopreview
