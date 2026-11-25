#include "EditorMcpRenderer.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/Base64.hpp"
#include "../../utils/FilesystemPath.hpp"
#include "../../utils/ImageConverter.hpp"
#include "../../utils/RenderTexture.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/DrawGridLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editormcp {

namespace {

constexpr int kMinDim = 64;
constexpr int kMaxDim = 3840;
constexpr float kMinZoom = 0.05f;
constexpr float kMaxZoom = 8.0f;
constexpr float kDesignHeight = 320.f;
constexpr float kGroundOffsetY = 90.0f;

bool hasGroupId(GameObject* obj, int groupId) {
    if (!obj || groupId <= 0) return false;
    for (int i = 0; i < obj->m_groupCount; ++i) {
        if (obj->getGroupID(i) == groupId) return true;
    }
    return false;
}

bool isRendererHazardId(int id) {
    switch (id) {
        case 8: case 9: case 39: case 103: case 144: case 145: case 216:
        case 88: case 89: case 98: case 1705: case 1706: case 1707:
            return true;
        default:
            return false;
    }
}

bool isRendererSolidId(int id) {
    if ((id >= 1 && id <= 7) || (id >= 467 && id <= 470) ||
        (id >= 69 && id <= 75) || (id >= 90 && id <= 96) || (id >= 160 && id <= 169)) {
        return true;
    }
    return id == 211 || id == 289 || id == 290 || id == 291 || id == 292 || id == 695 || id == 696;
}

float chooseCoordStep(float visibleSpan) {
    float const targetTicks = 7.f;
    float const raw = std::max(30.f, visibleSpan / targetTicks);
    static constexpr float kSteps[] = {30.f, 60.f, 90.f, 150.f, 300.f, 600.f, 1200.f, 2400.f};
    for (float s : kSteps) {
        if (raw <= s) return s;
    }
    return 2400.f;
}

struct ScopedOverlayGuard {
    std::vector<CCNode*> nodes;
    ~ScopedOverlayGuard() {
        for (auto* n : nodes) {
            if (n && n->getParent()) n->removeFromParentAndCleanup(true);
        }
    }
    void track(CCNode* parent, CCNode* child, int z) {
        if (!parent || !child) return;
        parent->addChild(child, z);
        nodes.push_back(child);
    }
};

void flipVerticalRgba(std::vector<uint8_t>& px, int w, int h) {
    size_t const rowBytes = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> tmp(rowBytes);
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* top = px.data() + static_cast<size_t>(y) * rowBytes;
        uint8_t* bot = px.data() + static_cast<size_t>(h - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, tmp.data(), rowBytes);
    }
}

struct EditorVisualGuard {
    LevelEditorLayer* lel = nullptr;
    CCPoint objPos{};
    float objScale = 1.f;
    bool uiVisible = true;
    bool gridVisible = true;
    bool previewMode = false;
    bool previewShaders = false;
    bool previewAnimations = false;
    bool drawTriggerBoxes = false;
    bool shaderSaved = false;
    CCSize shaderScreen{};
    CCSize shaderTarget{};
    bool shaderPixelate = false;

    explicit EditorVisualGuard(LevelEditorLayer* layer) : lel(layer) {
        if (!lel) return;
        if (lel->m_objectLayer) {
            objPos = lel->m_objectLayer->getPosition();
            objScale = lel->m_objectLayer->getScale();
        }
        if (lel->m_editorUI) {
            uiVisible = lel->m_editorUI->isVisible();
        }
        if (lel->m_drawGridLayer) {
            gridVisible = lel->m_drawGridLayer->isVisible();
        }
        previewMode = lel->m_previewMode;
        previewShaders = lel->m_previewShaders;
        previewAnimations = lel->m_previewAnimations;
        drawTriggerBoxes = lel->m_drawTriggerBoxes;
        if (auto* shader = lel->m_shaderLayer; shader && shader->getParent()) {
            shaderSaved = true;
            shaderScreen = shader->m_screenSize;
            shaderTarget = shader->m_targetTextureSize;
            shaderPixelate = shader->m_state.m_pixelateHardEdges;
        }
    }

    ~EditorVisualGuard() {
        if (!lel) return;
        if (lel->m_objectLayer) {
            lel->m_objectLayer->setScale(objScale);
            lel->m_objectLayer->setPosition(objPos);
        }
        if (lel->m_editorUI) {
            lel->m_editorUI->setVisible(uiVisible);
        }
        if (lel->m_drawGridLayer) {
            lel->m_drawGridLayer->setVisible(gridVisible);
        }
        lel->m_previewMode = previewMode;
        lel->m_previewShaders = previewShaders;
        lel->m_previewAnimations = previewAnimations;
        lel->m_drawTriggerBoxes = drawTriggerBoxes;

        if (shaderSaved && lel->m_shaderLayer) {
            auto* shader = lel->m_shaderLayer;
            shader->m_screenSize = shaderScreen;
            shader->m_targetTextureSize = shaderTarget;
            shader->setupShader(false);
            if (!shaderPixelate && shader->m_sprite && shader->m_sprite->getTexture()) {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                shader->m_sprite->getTexture()->setTexParameters(&params);
            }
            shader->prePixelateShader();
        }

        lel->updateVisibility(0.f);
        lel->updateLevelColors();
    }
};

} // namespace

RenderResult renderEditor1To1(RenderRequest const& req) {
    RenderResult res;
    if (paimon::isRuntimeShuttingDown()) {
        res.error = "Runtime is shutting down.";
        return res;
    }

    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objectLayer) {
        res.error = "No active LevelEditorLayer. Open a level in the Geometry Dash editor first.";
        return res;
    }

    auto* director = CCDirector::get();
    if (!director || !director->getOpenGLView()) {
        res.error = "OpenGL view unavailable.";
        return res;
    }

    int const W = std::clamp(req.width, kMinDim, kMaxDim);
    int const H = std::clamp(req.height, kMinDim, kMaxDim);
    CCSize const oldWinSize = director->getWinSize();
    if (oldWinSize.width <= 0.f || oldWinSize.height <= 0.f) {
        res.error = "Invalid director window size.";
        return res;
    }

    int const totalObjects = lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0;

    // Resolve target camera center & zoom in m_objectLayer space (internalY = worldY + 90).
    CCPoint currentCenter = lel->m_objectLayer->convertToNodeSpace(oldWinSize * 0.5f);
    float currentZoom = std::clamp(lel->m_objectLayer->getScale(), kMinZoom, kMaxZoom);

    float targetX = req.hasCenter ? req.centerX : currentCenter.x;
    float targetY = req.hasCenter
        ? (req.worldCoords ? (req.centerY + kGroundOffsetY) : req.centerY)
        : currentCenter.y;
    float targetZoom = std::clamp(req.zoom, kMinZoom, kMaxZoom);

    float const aspect = static_cast<float>(W) / static_cast<float>(H);
    float const designW = std::round(kDesignHeight * aspect);

    if (req.mode == "viewport") {
        if (!req.hasCenter) {
            targetX = currentCenter.x;
            targetY = currentCenter.y;
        }
        targetZoom = currentZoom;
    } else if (req.mode == "overview") {
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        bool hasAny = false;

        if (req.hasBounds) {
            float const bOff = req.worldCoords ? kGroundOffsetY : 0.f;
            minX = std::min(req.boundsMinX, req.boundsMaxX);
            maxX = std::max(req.boundsMinX, req.boundsMaxX);
            minY = std::min(req.boundsMinY, req.boundsMaxY) + bOff;
            maxY = std::max(req.boundsMinY, req.boundsMaxY) + bOff;
            hasAny = true;
        } else if (lel->m_objects) {
            for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
                if (!obj) continue;
                float const ox = obj->getPositionX();
                float const oy = obj->getPositionY();
                minX = std::min(minX, ox - 15.f);
                maxX = std::max(maxX, ox + 15.f);
                minY = std::min(minY, oy - 15.f);
                maxY = std::max(maxY, oy + 15.f);
                hasAny = true;
            }
        }

        if (!hasAny) {
            minX = -60.f; maxX = 480.f;
            minY = kGroundOffsetY; maxY = kGroundOffsetY + 300.f;
        }

        targetX = (minX + maxX) * 0.5f;
        targetY = (minY + maxY) * 0.5f;
        float const spanW = std::max(60.f, (maxX - minX) * 1.15f);
        float const spanH = std::max(60.f, (maxY - minY) * 1.15f);
        targetZoom = std::clamp(std::min(designW / spanW, kDesignHeight / spanH), kMinZoom, kMaxZoom);
    }

    EditorVisualGuard guard(lel);

    while (glGetError() != GL_NO_ERROR) {}
    RenderTexture rt(static_cast<uint32_t>(W), static_cast<uint32_t>(H));
    if (!rt.isValid() || !rt.begin()) {
        res.error = "Failed to initialize offscreen RenderTexture.";
        return res;
    }

    struct EndTarget {
        RenderTexture& target;
        ~EndTarget() { target.end(); }
    } endTarget{rt};

    CCSize const newWinSize = director->getWinSize();

    if (lel->m_editorUI) {
        lel->m_editorUI->setVisible(req.showUI);
    }
    if (lel->m_drawGridLayer) {
        lel->m_drawGridLayer->setVisible(req.showGrid);
    }
    lel->m_drawTriggerBoxes = req.showTriggerBoxes || req.showGrid;
    lel->m_previewMode = req.previewColors;
    if (req.previewColors) {
        lel->m_previewShaders = true;
        lel->m_previewAnimations = true;
    }

    // Position m_objectLayer so (targetX, targetY) sits at the exact center of the 1:1 viewport.
    lel->m_objectLayer->setScale(targetZoom);
    CCPoint const centerScreen{newWinSize.width * 0.5f, newWinSize.height * 0.5f};
    CCPoint const currentWorld = lel->m_objectLayer->convertToWorldSpace({targetX, targetY});
    lel->m_objectLayer->setPosition(lel->m_objectLayer->getPosition() + centerScreen - currentWorld);

    float const halfWorldW = (newWinSize.width * 0.5f) / targetZoom;
    float const halfWorldH = (newWinSize.height * 0.5f) / targetZoom;
    float const worldMinX = targetX - halfWorldW;
    float const worldMaxX = targetX + halfWorldW;
    float const internalMinY = targetY - halfWorldH;
    float const internalMaxY = targetY + halfWorldH;
    float const wyMin = internalMinY - kGroundOffsetY;
    float const wyMax = internalMaxY - kGroundOffsetY;

    // Resize shader layer if active so 2.2 shaders render at exact 1:1 output dimensions.
    if (auto* shader = lel->m_shaderLayer; shader && shader->getParent()) {
        CCSize const captureSize{static_cast<float>(W), static_cast<float>(H)};
        if (std::abs(shader->m_targetTextureSize.width - captureSize.width) > 0.5f ||
            std::abs(shader->m_targetTextureSize.height - captureSize.height) > 0.5f) {
            shader->m_screenSize = newWinSize;
            shader->m_targetTextureSize = captureSize;
            shader->setupShader(false);
            if (!shader->m_state.m_pixelateHardEdges && shader->m_sprite && shader->m_sprite->getTexture()) {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                shader->m_sprite->getTexture()->setTexParameters(&params);
            }
            shader->prePixelateShader();
        }
        lel->updateShaderLayer(0.f);
        rt.bind();
    }

    lel->updateLevelColors();
    lel->updateVisibility(0.f);

    ScopedOverlayGuard overlays;
    if (req.showHitboxes || req.highlightGroup > 0) {
        if (auto* worldDraw = CCDrawNode::create()) {
            overlays.track(lel->m_objectLayer, worldDraw, 9998);
            float const lineW = std::clamp(1.1f / targetZoom, 0.35f, 3.0f);
            constexpr ccColor4F kNoFill{0.f, 0.f, 0.f, 0.f};
            if (lel->m_objects) {
                for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
                    if (!obj || obj->m_isTrigger) continue;
                    float const ox = obj->getPositionX();
                    float const oy = obj->getPositionY();
                    if (ox < worldMinX - 60.f || ox > worldMaxX + 60.f ||
                        oy < internalMinY - 60.f || oy > internalMaxY + 60.f) {
                        continue;
                    }
                    float const hw = 15.f * std::max(0.1f, std::abs(obj->m_scaleX));
                    float const hh = 15.f * std::max(0.1f, std::abs(obj->m_scaleY));
                    if (req.highlightGroup > 0 && hasGroupId(obj, req.highlightGroup)) {
                        worldDraw->drawRect(
                            CCRect{ox - hw - 1.5f, oy - hh - 1.5f, (hw + 1.5f) * 2.f, (hh + 1.5f) * 2.f},
                            kNoFill, lineW * 1.4f, {0.f, 0.95f, 1.f, 0.95f}, BorderAlignment::Outside
                        );
                    }
                    if (req.showHitboxes) {
                        int const id = obj->m_objectID;
                        if (isRendererHazardId(id) && !obj->m_isNoTouch) {
                            worldDraw->drawRect(
                                CCRect{ox - hw * 0.45f, oy - hh * 0.45f, hw * 0.9f, hh * 0.9f},
                                {1.f, 0.15f, 0.2f, 0.14f}, lineW, {1.f, 0.2f, 0.25f, 0.95f}, BorderAlignment::Inside
                            );
                        } else if (isRendererSolidId(id) && !obj->m_isNoTouch) {
                            worldDraw->drawRect(
                                CCRect{ox - hw, oy - hh, hw * 2.f, hh * 2.f},
                                kNoFill, lineW, {0.2f, 0.95f, 0.35f, 0.85f}, BorderAlignment::Inside
                            );
                        } else if (!obj->m_isNoTouch && !obj->m_isHide &&
                                   (static_cast<int>(obj->m_zLayer) != 0 || std::abs(obj->m_scaleX - 1.f) > 0.05f || std::abs(obj->m_scaleY - 1.f) > 0.05f)) {
                            worldDraw->drawRect(
                                CCRect{ox - hw, oy - hh, hw * 2.f, hh * 2.f},
                                kNoFill, lineW, {1.f, 0.65f, 0.1f, 0.9f}, BorderAlignment::Inside
                            );
                        }
                    }
                }
            }
        }
    }

    if (req.showCoords) {
        if (auto* worldAxis = CCDrawNode::create()) {
            overlays.track(lel->m_objectLayer, worldAxis, 9997);
            if (wyMin <= 0.f && wyMax >= 0.f) {
                worldAxis->drawSegment(
                    {worldMinX, kGroundOffsetY},
                    {worldMaxX, kGroundOffsetY},
                    std::clamp(0.8f / targetZoom, 0.25f, 2.0f),
                    {0.1f, 0.95f, 1.f, 0.65f}
                );
            }
        }
        if (auto* hudNode = CCNode::create()) {
            overlays.track(lel, hudNode, 9999);
            auto* rulerDraw = CCDrawNode::create();
            if (rulerDraw) {
                hudNode->addChild(rulerDraw, 0);
                // Semi-transparent edge strips for coordinate legibility
                rulerDraw->drawRect(CCRect{0.f, 0.f, newWinSize.width, 14.f}, {0.f, 0.f, 0.f, 0.55f}, 0.f, {0.f, 0.f, 0.f, 0.f}, BorderAlignment::Inside);
                rulerDraw->drawRect(CCRect{0.f, 14.f, 28.f, newWinSize.height - 14.f}, {0.f, 0.f, 0.f, 0.55f}, 0.f, {0.f, 0.f, 0.f, 0.f}, BorderAlignment::Inside);

                float const stepX = chooseCoordStep(worldMaxX - worldMinX);
                float const startX = std::ceil(worldMinX / stepX) * stepX;
                for (float wx = startX; wx <= worldMaxX; wx += stepX) {
                    float const sx = (wx - worldMinX) * targetZoom;
                    if (sx < 32.f || sx > newWinSize.width - 14.f) continue;
                    rulerDraw->drawSegment({sx, 0.f}, {sx, 14.f}, 0.6f, {0.35f, 0.95f, 1.f, 0.8f});
                    if (auto* lbl = CCLabelBMFont::create(fmt::format("{}", static_cast<int>(std::llround(wx))).c_str(), "chatFont.fnt")) {
                        lbl->setScale(0.38f);
                        lbl->setColor({180, 245, 255});
                        lbl->setPosition({sx + 2.f, 7.f});
                        lbl->setAnchorPoint({0.f, 0.5f});
                        hudNode->addChild(lbl, 1);
                    }
                }

                float const stepY = chooseCoordStep(wyMax - wyMin);
                float const startY = std::ceil(wyMin / stepY) * stepY;
                for (float wy = startY; wy <= wyMax; wy += stepY) {
                    float const sy = (wy - wyMin) * targetZoom;
                    if (sy < 18.f || sy > newWinSize.height - 8.f) continue;
                    bool const isGround = std::abs(wy) < 0.5f;
                    ccColor4F const tickCol = isGround ? ccColor4F{0.2f, 1.f, 0.8f, 0.95f} : ccColor4F{0.95f, 0.9f, 0.45f, 0.8f};
                    rulerDraw->drawSegment({0.f, sy}, {28.f, sy}, 0.6f, tickCol);
                    if (auto* lbl = CCLabelBMFont::create(fmt::format("{}", static_cast<int>(std::llround(wy))).c_str(), "chatFont.fnt")) {
                        lbl->setScale(0.38f);
                        lbl->setColor(isGround ? ccColor3B{120, 255, 220} : ccColor3B{255, 240, 160});
                        lbl->setPosition({2.f, sy + 1.f});
                        lbl->setAnchorPoint({0.f, 0.f});
                        hudNode->addChild(lbl, 1);
                    }
                }
            }
        }
    }

    lel->visit();

    auto rawPixels = rt.getData();
    if (!rawPixels) {
        res.error = "OpenGL glReadPixels failed.";
        return res;
    }

    int visibleCount = 0;
    if (lel->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            float const ox = obj->getPositionX();
            float const oy = obj->getPositionY();
            if (ox >= worldMinX - 60.f && ox <= worldMaxX + 60.f &&
                oy >= internalMinY - 60.f && oy <= internalMaxY + 60.f) {
                ++visibleCount;
            }
        }
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
    std::memcpy(rgba.data(), rawPixels.get(), rgba.size());
    flipVerticalRgba(rgba, W, H);

    std::vector<uint8_t> png;
    if (!ImageConverter::rgbaToPngBuffer(rgba.data(), static_cast<uint32_t>(W), static_cast<uint32_t>(H), png)) {
        res.error = "PNG encoding failed.";
        return res;
    }

    if (!req.savePath.empty()) {
        auto writeRes = geode::utils::file::writeBinary(paimon::paths::toSdkPath(req.savePath), png);
        if (writeRes.isOk()) {
            res.savedPath = req.savePath;
        }
    }

    res.pngBase64 = paimon::base64Encode(png);
    res.pngBytes = std::move(png);
    res.width = W;
    res.height = H;
    res.centerX = targetX;
    res.centerY = targetY - kGroundOffsetY;
    res.zoom = targetZoom;
    res.worldMinX = worldMinX;
    res.worldMaxX = worldMaxX;
    res.worldMinY = wyMin;
    res.worldMaxY = wyMax;
    res.pixelsPerUnit = targetZoom * (static_cast<float>(H) / kDesignHeight);
    res.visibleObjectCount = visibleCount;
    res.totalObjectCount = totalObjects;
    res.ok = true;
    return res;
}

} // namespace paimon::editormcp
