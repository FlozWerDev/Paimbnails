#include "OffscreenLevelRenderer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>

#include "PreviewZonePicker.hpp"
#include "AutoPreviewGenerator.hpp"   // downscalergba
#include "../AutoPreviewConfig.hpp"
#include "../../../utils/RenderTexture.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../framework/compat/ModCompat.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

namespace {

constexpr size_t kMaxLevelStringBytes = 3 * 1024 * 1024; // hard ~3MB pre-guard
constexpr size_t kUnknownCountMaxBytes = 512 * 1024;    // strict cap when m_objectCount unknown

std::atomic<bool>& renderFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

struct FlagReset {
    ~FlagReset() { renderFlag().store(false, std::memory_order_relaxed); }
};

// PlayLayer::create mutates gm->m_playLayer as a side effect; restore it on
// every exit path (early-return, throw, success) so a real session never
// keeps pointing at the offscreen layer.
struct PlayLayerGuard {
    GameManager* gm = nullptr;
    PlayLayer* prev = nullptr;
    explicit PlayLayerGuard(GameManager* g) : gm(g), prev(g ? g->m_playLayer : nullptr) {}
    ~PlayLayerGuard() { if (gm) gm->m_playLayer = prev; }
    PlayLayerGuard(PlayLayerGuard const&) = delete;
    PlayLayerGuard& operator=(PlayLayerGuard const&) = delete;
};

constexpr float kDesignH = 320.f;          // gd camera height in world units (zoom 1)
constexpr int   kSupersample = 2;          // render at nx then downscale for aa/sharpness

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
// only objects inside the capture window (fbo has no camera progress to unhide mid-level ones).
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
    if (pl->m_objectLayer) {
        pl->m_objectLayer->setPosition(-camLeftX, -camBottomY);
    }
    return shown;
}

bool renderWindow(CCNode* scene, int rw, int rh, std::vector<uint8_t>& out) {
    RenderTexture rt(static_cast<uint32_t>(rw), static_cast<uint32_t>(rh));
    if (!rt.begin()) return false;
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

bool OffscreenLevelRenderer::isRendering() {
    return renderFlag().load(std::memory_order_relaxed);
}

bool OffscreenLevelRenderer::passesSizeGuards(GJGameLevel* level, int levelID) {
    size_t const levelSize = level->m_levelString.size();
    long const objCount = static_cast<long>(level->m_objectCount.value());
    long const cap = static_cast<long>(config::maxObjects());
    if (levelSize > kMaxLevelStringBytes) {
        log::debug("[AutoPreview] skip level {} (size {} > {} bytes, objects {})",
                   levelID, levelSize, kMaxLevelStringBytes, objCount);
        return false;
    }
    if (objCount <= 0) {
        // unknown object count: only allow small strings; the renderer's
        // post-create guard re-checks the real count after PlayLayer::create.
        if (levelSize > kUnknownCountMaxBytes) {
            log::debug("[AutoPreview] skip level {} (unknown objects, size {} bytes)",
                       levelID, levelSize);
            return false;
        }
    } else if (objCount > cap) {
        log::debug("[AutoPreview] skip level {} ({} objects > cap {})",
                   levelID, objCount, cap);
        return false;
    }
    return true;
}

OffscreenRenderResult OffscreenLevelRenderer::render(GJGameLevel* level, int width, int height) {
OffscreenRenderResult result;
    if (!level || width <= 0 || height <= 0) return result;
    if (paimon::isRuntimeShuttingDown()) return result;

    int const levelID = level->m_levelID.value();
    size_t const levelSize = level->m_levelString.size();
    long const objCount = static_cast<long>(level->m_objectCount.value());
    long const cap = static_cast<long>(config::maxObjects());

    auto* dir = CCDirector::get();
    if (!dir || !dir->getOpenGLView()) return result;
    if (PlayLayer::get() != nullptr) {
        log::debug("[AutoPreview] skip level {} (PlayLayer active, objects {}, size {})",
                   levelID, objCount, levelSize);
        return result;
    }
    if (LevelEditorLayer::get() != nullptr) {
        log::debug("[AutoPreview] skip level {} (LevelEditor active, objects {}, size {})",
                   levelID, objCount, levelSize);
        return result;
    }
    if (level->m_levelString.empty()) return result;
    if (!passesSizeGuards(level, levelID)) return result;

    if (renderFlag().exchange(true, std::memory_order_acquire)) {
        log::debug("[AutoPreview] skip level {} (render already in progress, objects {}, size {})",
                   levelID, objCount, levelSize);
        return result;
    }
    FlagReset resetFlag;

    // playlayer::create runs every mod's init hook; skip or globed announces the level online.
    if (paimon::compat::ModCompat::isGlobedLoaded()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            log::info("[AutoPreview] Globed loaded; skipping offscreen level render "
                      "(it would register as joining the level online)");
        }
        return result;
    }

    auto* gm = GameManager::get();
    PlayLayerGuard playLayerGuard(gm);

    PlayLayer* pl = nullptr;
    try {
        pl = PlayLayer::create(level, false);
    } catch (std::exception const& ex) {
        log::debug("[AutoPreview] skip level {} (PlayLayer::create threw: {}, objects {}, size {})",
                   levelID, ex.what(), objCount, levelSize);
        return result;
    } catch (...) {
        log::debug("[AutoPreview] skip level {} (PlayLayer::create threw unknown, objects {}, size {})",
                   levelID, objCount, levelSize);
        return result;
    }
    if (!pl) {
        return result;
    }

    // post-create guard: m_objectCount==0 levels can expand past the cap.
    {
        int const actualCount = pl->m_objects ? pl->m_objects->count() : 0;
        if (actualCount > cap) {
            log::debug("[AutoPreview] skip level {} (post-create {} objects > cap {}, size {})",
                       levelID, actualCount, cap, levelSize);
            return result;
        }
    }

    Ref<CCScene> scene = CCScene::create();
    if (!scene) {
        return result;
    }
    try {
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

        // attempt 2 (fallback): start of the level.
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

    } catch (std::exception const& ex) {
        log::debug("[AutoPreview] skip level {} (offscreen render threw: {}, objects {}, size {})",
                   levelID, ex.what(), objCount, levelSize);
        scene->removeChild(pl, true);
        return result;
    } catch (...) {
        log::debug("[AutoPreview] skip level {} (offscreen render threw unknown, objects {}, size {})",
                   levelID, objCount, levelSize);
        scene->removeChild(pl, true);
        return result;
    }

    scene->removeChild(pl, true);
    return result;
}

} // namespace paimon::autopreview
