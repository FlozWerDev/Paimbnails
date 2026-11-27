#include "EditorObjectClassifier.hpp"

#include "../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJSpriteColor.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::optimizer {

namespace {

constexpr int kSampleSide = 32;
constexpr unsigned char kAlphaThreshold = 24;
constexpr int kTextObjectID = 914;
constexpr int kCounterObjectID = 1615;
constexpr int kWhiteSquareBlockID = 211;

constexpr char const* kMaxColKey = "editor-optimizer-max-col";
constexpr char const* kMaxDecoKey = "editor-optimizer-max-deco";
constexpr char const* kPreserveCornersKey = "editor-optimizer-preserve-corners";
constexpr char const* kMerge2DKey = "editor-optimizer-merge-2d";

struct BatchPool {
    BatchPool() { CCPoolManager::sharedPoolManager()->push(); }
    ~BatchPool() { CCPoolManager::sharedPoolManager()->pop(); }
};

struct SpriteCapabilityInfo {
    StretchCapability stretch = StretchCapability::None;
    bool rot90Sym = false;
};

std::unordered_map<int, SpriteCapabilityInfo>& capabilityCache() {
    static auto& cache = *new std::unordered_map<int, SpriteCapabilityInfo>();
    return cache;
}

std::string toLower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char ch : sv) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool hasNonStretchFrameKeyword(std::string const& lowerName) {
    static constexpr std::array<char const*, 29> kBlockedKeywords{{
        "corner", "slope", "spike", "saw", "gear", "wheel", "circle", "ring",
        "orb", "portal", "pad", "coin", "arrow", "arch", "curve", "round",
        "diag", "tri", "star", "heart", "skull", "chain", "bush", "tree",
        "rock", "cloud", "rod", "thorn", "monster",
    }};
    for (char const* kw : kBlockedKeywords) {
        if (lowerName.find(kw) != std::string::npos) return true;
    }
    return false;
}

float pixelDiff(unsigned char const* rgba, int idxA, int idxB) {
    float const dr = std::abs(static_cast<int>(rgba[idxA + 0]) - static_cast<int>(rgba[idxB + 0])) / 255.f;
    float const dg = std::abs(static_cast<int>(rgba[idxA + 1]) - static_cast<int>(rgba[idxB + 1])) / 255.f;
    float const db = std::abs(static_cast<int>(rgba[idxA + 2]) - static_cast<int>(rgba[idxB + 2])) / 255.f;
    float const da = std::abs(static_cast<int>(rgba[idxA + 3]) - static_cast<int>(rgba[idxB + 3])) / 255.f;
    return (dr + dg + db + da * 1.5f) * 0.25f;
}

SpriteCapabilityInfo analyzeSpritePixels(int objectID, GameObject* sampleObject) {
    if (objectID == kWhiteSquareBlockID) return {StretchCapability::Uniform2D, true};
    if (objectID <= 0 || objectID == kTextObjectID || objectID == kCounterObjectID) {
        return {};
    }

    if (auto* toolbox = ObjectToolbox::sharedState()) {
        if (char const* frameName = toolbox->intKeyToFrame(objectID)) {
            if (hasNonStretchFrameKeyword(toLower(frameName))) {
                return {};
            }
        }
    }

    BatchPool const pool;
    GameObject* probe = GameObject::createWithKey(objectID);
    if (!probe) probe = sampleObject;
    if (!probe) return {};
    auto* frame = probe->displayFrame();
    if (!frame) return {};

    auto const content = probe->getContentSize();
    if (content.width < 0.5f || content.height < 0.5f ||
        std::max(content.width, content.height) < 4.f ||
        content.width > 256.f || content.height > 256.f) {
        return {};
    }

    auto* canvas = CCRenderTexture::create(
        kSampleSide, kSampleSide, kCCTexture2DPixelFormat_RGBA8888
    );
    if (!canvas) return {};

    auto* baseSprite = CCSprite::createWithSpriteFrame(frame);
    if (!baseSprite) return {};
    auto const baseContent = baseSprite->getContentSize();
    if (baseContent.width < 0.5f || baseContent.height < 0.5f) return {};

    float const scaleX = kSampleSide / content.width;
    float const scaleY = kSampleSide / content.height;

    baseSprite->setAnchorPoint({0.5f, 0.5f});
    baseSprite->setScaleX(scaleX);
    baseSprite->setScaleY(scaleY);
    baseSprite->setPosition({kSampleSide * 0.5f, kSampleSide * 0.5f});
    baseSprite->setColor({255, 255, 255});
    baseSprite->setOpacity(255);

    CCSprite* detailSprite = nullptr;
    if (auto* detail = probe->m_colorSprite; detail && detail->isVisible()) {
        if (auto* detailFrame = detail->displayFrame()) {
            detailSprite = CCSprite::createWithSpriteFrame(detailFrame);
            if (detailSprite) {
                detailSprite->setAnchorPoint(detail->getAnchorPoint());
                detailSprite->setScaleX(scaleX * detail->getScaleX());
                detailSprite->setScaleY(scaleY * detail->getScaleY());
                detailSprite->setRotation(detail->getRotation());
                detailSprite->setFlipX(detail->isFlipX());
                detailSprite->setFlipY(detail->isFlipY());
                auto const rel = detail->getPosition() - content * 0.5f;
                detailSprite->setPosition({
                    kSampleSide * 0.5f + rel.x * scaleX,
                    kSampleSide * 0.5f + rel.y * scaleY,
                });
                detailSprite->setColor({160, 200, 255});
                detailSprite->setOpacity(255);
            }
        }
    }

    // Also collect any extra visible CCSprite children (e.g. inner patterns/black fills)
    // so blocks with non-stretchable internal child artwork are classified accurately.
    std::vector<CCSprite*> extraSprites;
    if (auto* children = probe->getChildren()) {
        for (auto* child : CCArrayExt<CCObject*>(children)) {
            auto* sp = typeinfo_cast<CCSprite*>(child);
            if (!sp || !sp->isVisible() || sp == probe->m_colorSprite || sp == probe->m_glowSprite) {
                continue;
            }
            if (auto* childFrame = sp->displayFrame()) {
                if (auto* copy = CCSprite::createWithSpriteFrame(childFrame)) {
                    copy->setAnchorPoint(sp->getAnchorPoint());
                    copy->setScaleX(scaleX * sp->getScaleX());
                    copy->setScaleY(scaleY * sp->getScaleY());
                    copy->setRotation(sp->getRotation());
                    copy->setFlipX(sp->isFlipX());
                    copy->setFlipY(sp->isFlipY());
                    auto const rel = sp->getPosition() - content * 0.5f;
                    copy->setPosition({
                        kSampleSide * 0.5f + rel.x * scaleX,
                        kSampleSide * 0.5f + rel.y * scaleY,
                    });
                    copy->setColor(sp->getColor());
                    copy->setOpacity(sp->getOpacity());
                    extraSprites.push_back(copy);
                }
            }
        }
    }

    canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
    baseSprite->visit();
    if (detailSprite) detailSprite->visit();
    for (auto* extra : extraSprites) extra->visit();
    canvas->end();

    auto* image = canvas->newCCImage(true);
    if (!image) return {};
    auto const* rgba = image->getData();
    int const w = image->getWidth();
    int const h = image->getHeight();
    if (!rgba || w < kSampleSide || h < kSampleSide) {
        image->release();
        return {};
    }

    int covered = 0;
    for (int y = 0; y < kSampleSide; ++y) {
        for (int x = 0; x < kSampleSide; ++x) {
            int const idx = (y * w + x) * 4;
            if (rgba[idx + 3] >= kAlphaThreshold) ++covered;
        }
    }
    if (covered < 16) {
        image->release();
        return {};
    }

    // Check left-right and top-bottom symmetry normalized over non-transparent pixel pairs
    // so thin edge outlines are not diluted by empty background rows/columns.
    double symXSum = 0.0;
    int symXPairs = 0;
    double symYSum = 0.0;
    int symYPairs = 0;
    for (int y = 0; y < kSampleSide; ++y) {
        for (int x = 0; x < kSampleSide / 2; ++x) {
            int const idxL = (y * w + x) * 4;
            int const idxR = (y * w + (kSampleSide - 1 - x)) * 4;
            if (rgba[idxL + 3] >= kAlphaThreshold || rgba[idxR + 3] >= kAlphaThreshold) {
                symXSum += pixelDiff(rgba, idxL, idxR);
                ++symXPairs;
            }
        }
    }
    for (int y = 0; y < kSampleSide / 2; ++y) {
        for (int x = 0; x < kSampleSide; ++x) {
            int const idxT = (y * w + x) * 4;
            int const idxB = ((kSampleSide - 1 - y) * w + x) * 4;
            if (rgba[idxT + 3] >= kAlphaThreshold || rgba[idxB + 3] >= kAlphaThreshold) {
                symYSum += pixelDiff(rgba, idxT, idxB);
                ++symYPairs;
            }
        }
    }
    float const symX = static_cast<float>(symXSum / std::max(symXPairs, 1));
    float const symY = static_cast<float>(symYSum / std::max(symYPairs, 1));

    // Check interior uniformity across middle columns [6..25] and middle rows [6..25].
    // Track both global mean and worst-row/worst-col variance so small centered emblems
    // or corner rivets are not diluted across 32 rows/columns.
    constexpr int kMidStart = 6;
    constexpr int kMidEnd = 25;
    double varXSum = 0.0;
    int varXCount = 0;
    float maxRowVarX = 0.f;
    for (int y = 0; y < kSampleSide; ++y) {
        int const refIdx = (y * w + 16) * 4;
        double rowSum = 0.0;
        int rowCnt = 0;
        for (int x = kMidStart; x <= kMidEnd; ++x) {
            int const curIdx = (y * w + x) * 4;
            if (rgba[curIdx + 3] >= kAlphaThreshold || rgba[refIdx + 3] >= kAlphaThreshold) {
                float const d = pixelDiff(rgba, curIdx, refIdx);
                varXSum += d;
                ++varXCount;
                rowSum += d;
                ++rowCnt;
            }
        }
        if (rowCnt > 0) {
            maxRowVarX = std::max(maxRowVarX, static_cast<float>(rowSum / rowCnt));
        }
    }
    double varYSum = 0.0;
    int varYCount = 0;
    float maxColVarY = 0.f;
    for (int x = 0; x < kSampleSide; ++x) {
        int const refIdx = (16 * w + x) * 4;
        double colSum = 0.0;
        int colCnt = 0;
        for (int y = kMidStart; y <= kMidEnd; ++y) {
            int const curIdx = (y * w + x) * 4;
            if (rgba[curIdx + 3] >= kAlphaThreshold || rgba[refIdx + 3] >= kAlphaThreshold) {
                float const d = pixelDiff(rgba, curIdx, refIdx);
                varYSum += d;
                ++varYCount;
                colSum += d;
                ++colCnt;
            }
        }
        if (colCnt > 0) {
            maxColVarY = std::max(maxColVarY, static_cast<float>(colSum / colCnt));
        }
    }
    float const interiorVarX = static_cast<float>(varXSum / std::max(varXCount, 1));
    float const interiorVarY = static_cast<float>(varYSum / std::max(varYCount, 1));

    // Check full-box uniformity (e.g. solid flat color square or slab).
    int const centerIdx = (16 * w + 16) * 4;
    double allVarSum = 0.0;
    float maxCenterDiff = 0.f;
    for (int y = 0; y < kSampleSide; ++y) {
        for (int x = 0; x < kSampleSide; ++x) {
            float const d = pixelDiff(rgba, (y * w + x) * 4, centerIdx);
            allVarSum += d;
            if (x >= 1 && x <= kSampleSide - 2 && y >= 1 && y <= kSampleSide - 2) {
                maxCenterDiff = std::max(maxCenterDiff, d);
            }
        }
    }
    float const allVar = static_cast<float>(allVarSum / (kSampleSide * kSampleSide));

    // Check 90-degree rotational symmetry for square objects so 0/90/180/270 rotations can merge.
    double rot90Sum = 0.0;
    int rot90Pairs = 0;
    for (int y = 0; y < kSampleSide; ++y) {
        for (int x = 0; x < kSampleSide; ++x) {
            int const idxA = (y * w + x) * 4;
            int const idxB = (x * w + (kSampleSide - 1 - y)) * 4;
            if (rgba[idxA + 3] >= kAlphaThreshold || rgba[idxB + 3] >= kAlphaThreshold) {
                rot90Sum += pixelDiff(rgba, idxA, idxB);
                ++rot90Pairs;
            }
        }
    }
    float const rot90Diff = static_cast<float>(rot90Sum / std::max(rot90Pairs, 1));

    // Reject rounded/chamfered/beveled corners on stretchable axes: if the center edge row/col
    // is opaque while the corner pixel is empty, stretching along that axis elongates the curve.
    auto alphaAt = [&](int x, int y) { return rgba[(y * w + x) * 4 + 3]; };
    bool const cornerGapX =
        (alphaAt(16, 1) > 40 && alphaAt(1, 1) < 20) ||
        (alphaAt(16, 30) > 40 && alphaAt(1, 30) < 20) ||
        (alphaAt(16, 1) > 40 && alphaAt(30, 1) < 20) ||
        (alphaAt(16, 30) > 40 && alphaAt(30, 30) < 20);
    bool const cornerGapY =
        (alphaAt(1, 16) > 40 && alphaAt(1, 1) < 20) ||
        (alphaAt(30, 16) > 40 && alphaAt(30, 1) < 20) ||
        (alphaAt(1, 16) > 40 && alphaAt(1, 30) < 20) ||
        (alphaAt(30, 16) > 40 && alphaAt(30, 30) < 20);

    image->release();

    bool const squareAspect = std::abs(content.width - content.height) <= 2.f && content.width >= 14.f;
    bool const flatBox = (allVar <= 0.03f) &&
                         (maxCenterDiff <= 0.08f) &&
                         (covered >= static_cast<int>(kSampleSide * kSampleSide * 0.90f)) &&
                         (content.width >= 8.f && content.height >= 8.f);
    if (flatBox) {
        return {StretchCapability::Uniform2D, squareAspect};
    }

    bool canX = (symX <= 0.07f) && (interiorVarX <= 0.06f) && (maxRowVarX <= 0.18f) &&
                !cornerGapX && (content.width >= 12.f);
    bool canY = (symY <= 0.07f) && (interiorVarY <= 0.06f) && (maxColVarY <= 0.18f) &&
                !cornerGapY && (content.height >= 12.f);

    if (content.width > content.height * 1.8f) canY = false;
    if (content.height > content.width * 1.8f) canX = false;

    if (canX && canY) return {StretchCapability::Axis1D, squareAspect && rot90Diff <= 0.06f};
    if (canX) return {StretchCapability::HorizontalOnly, false};
    if (canY) return {StretchCapability::VerticalOnly, false};
    return {};
}

SpriteCapabilityInfo classifyObjectInfo(int objectID, GameObject* sampleObject) {
    auto& cache = capabilityCache();
    if (auto it = cache.find(objectID); it != cache.end()) {
        return it->second;
    }
    auto const result = analyzeSpritePixels(objectID, sampleObject);
    cache.emplace(objectID, result);
    return result;
}

float normalizeRotation(float deg) {
    float r = std::fmod(deg, 360.f);
    if (r < 0.f) r += 360.f;
    if (std::abs(r - 360.f) < 0.05f) r = 0.f;
    return r;
}

float canonicalizeRotation(
    float normRot,
    StretchCapability stretch,
    bool rot90Sym,
    float baseWidth,
    float baseHeight
) {
    bool const squareBase = std::abs(baseWidth - baseHeight) <= 0.2f;
    if ((stretch == StretchCapability::Uniform2D || stretch == StretchCapability::Axis1D) &&
        rot90Sym && squareBase) {
        float r = std::fmod(normRot, 90.f);
        if (std::abs(r - 90.f) < 0.05f) r = 0.f;
        return r;
    }
    if (stretch == StretchCapability::Uniform2D || stretch == StretchCapability::Axis1D) {
        float r = std::fmod(normRot, 180.f);
        if (std::abs(r - 180.f) < 0.05f) r = 0.f;
        return r;
    }
    return normRot;
}

std::string colorSignature(GJSpriteColor const* color) {
    if (!color) return "none";
    if (!color->m_usesHSV) {
        return fmt::format("{}/{}:{:.2f}/{:.2f}:{}:{},{},{}",
                           color->m_colorID, color->m_defaultColorID,
                           color->m_opacity, color->m_baseOpacity,
                           color->m_usesCustomBlend ? 1 : 0,
                           color->m_customColor.r, color->m_customColor.g, color->m_customColor.b);
    }
    return fmt::format("{}/{}:{:.2f}/{:.2f}:{}:{},{},{}:{:.2f},{:.2f},{:.2f},{},{}",
                       color->m_colorID, color->m_defaultColorID,
                       color->m_opacity, color->m_baseOpacity,
                       color->m_usesCustomBlend ? 1 : 0,
                       color->m_customColor.r, color->m_customColor.g, color->m_customColor.b,
                       color->m_hsv.h, color->m_hsv.s, color->m_hsv.v,
                       color->m_hsv.absoluteSaturation ? 1 : 0,
                       color->m_hsv.absoluteBrightness ? 1 : 0);
}

std::string groupsSignature(GameObject* obj) {
    if (!obj || obj->m_groupCount <= 0 || !obj->m_groups) return "";
    std::vector<int> groups;
    groups.reserve(static_cast<std::size_t>(obj->m_groupCount));
    for (int i = 0; i < obj->m_groupCount; ++i) {
        int const g = obj->getGroupID(i);
        if (g > 0) groups.push_back(g);
    }
    std::sort(groups.begin(), groups.end());
    std::string out;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (i) out += '.';
        out += std::to_string(groups[i]);
    }
    return out;
}

std::string buildCompatibilityKey(
    GameObject* obj,
    float normRot,
    StretchCapability stretch,
    bool rot90Sym,
    float baseWidth,
    float baseHeight
) {
    float const canonRot = canonicalizeRotation(normRot, stretch, rot90Sym, baseWidth, baseHeight);
    int const rotBucket = static_cast<int>(std::lround(canonRot * 20.f)); // 0.05 deg resolution
    bool const ignoreFlipX =
        stretch == StretchCapability::Uniform2D ||
        stretch == StretchCapability::Axis1D ||
        stretch == StretchCapability::HorizontalOnly;
    bool const ignoreFlipY =
        stretch == StretchCapability::Uniform2D ||
        stretch == StretchCapability::Axis1D ||
        stretch == StretchCapability::VerticalOnly;
    int const flipXKey = ignoreFlipX ? 0 : (obj->isFlipX() ? 1 : 0);
    int const flipYKey = ignoreFlipY ? 0 : (obj->isFlipY() ? 1 : 0);

    std::uint32_t const flags =
        (obj->m_isHighDetail ? 1u : 0u) |
        ((obj->m_isNoTouch ? 1u : 0u) << 1) |
        ((obj->m_isPassable ? 1u : 0u) << 2) |
        ((obj->m_isHide ? 1u : 0u) << 3) |
        ((obj->m_isInvisibleBlock ? 1u : 0u) << 4) |
        ((obj->m_hasNoGlow ? 1u : 0u) << 5) |
        ((obj->m_isDontFade ? 1u : 0u) << 6) |
        ((obj->m_isDontEnter ? 1u : 0u) << 7) |
        ((obj->m_isIceBlock ? 1u : 0u) << 8) |
        ((obj->m_isNonStickX ? 1u : 0u) << 9) |
        ((obj->m_isNonStickY ? 1u : 0u) << 10) |
        ((obj->m_isScaleStick ? 1u : 0u) << 11) |
        ((obj->m_isExtraSticky ? 1u : 0u) << 12) |
        ((obj->m_isDontBoostX ? 1u : 0u) << 13) |
        ((obj->m_isDontBoostY ? 1u : 0u) << 14) |
        ((obj->m_hasNoEffects ? 1u : 0u) << 15) |
        ((obj->m_hasNoParticles ? 1u : 0u) << 16) |
        ((obj->m_shouldBlendBase ? 1u : 0u) << 17) |
        ((obj->m_shouldBlendDetail ? 1u : 0u) << 18) |
        ((obj->m_isObjectBlack ? 1u : 0u) << 19) |
        ((obj->m_isColorSpriteBlack ? 1u : 0u) << 20) |
        ((obj->m_zFixedZLayer ? 1u : 0u) << 21) |
        ((obj->m_hasExtendedCollision ? 1u : 0u) << 22) |
        ((obj->m_isDisabled ? 1u : 0u) << 23) |
        ((obj->m_isInvisible ? 1u : 0u) << 24);

    return fmt::format(
        "{}|{}|{}|{},{}|{},{}|{},{}|{:x}|{}|{}|{}|{},{},{},{:.2f}|{}|{}|{}|{}",
        obj->m_objectID,
        static_cast<int>(obj->m_objectType),
        rotBucket,
        flipXKey,
        flipYKey,
        static_cast<int>(obj->m_zLayer),
        obj->m_zOrder,
        obj->m_editorLayer,
        obj->m_editorLayer2,
        flags,
        colorSignature(obj->m_baseColor),
        colorSignature(obj->m_detailColor),
        groupsSignature(obj),
        obj->m_enterChannel,
        obj->m_enterType,
        obj->m_exitType,
        obj->m_fadeMargin,
        obj->m_objectMaterial,
        obj->m_colorType,
        obj->m_customColorType,
        obj->m_parentMode
    );
}

bool isEligibleObject(
    LevelEditorLayer* editor,
    GameObject* obj,
    std::unordered_set<GameObject*> const& liveObjects
) {
    // Check membership in editor->getAllObjects() rather than getParent() because GD detaches
    // offscreen objects from their batch node when zoomed/panned in Full Level mode.
    if (!obj || !liveObjects.contains(obj)) return false;
    if (obj->m_isTrigger || obj->m_isStartPos || obj->m_hasParticles) return false;
    if (obj->m_hasGroupParent || obj->m_hasAreaParent || obj->m_linkedGroup > 0) return false;
    if (obj->m_hasAnimatedChild || obj->m_isUIObject) return false;
    if ((obj->m_usesAudioScale && !obj->m_hasNoAudioScale) || obj->m_customAudioScale) return false;
    if (obj->m_objectType != GameObjectType::Solid &&
        obj->m_objectType != GameObjectType::Decoration) {
        return false;
    }
    if (editor && editor->isLayerLocked(obj->m_editorLayer)) return false;
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjectLayer(obj)) return false;
    if (obj->m_scaleX <= 0.01f || obj->m_scaleY <= 0.01f) return false;
    if (!std::isfinite(obj->m_scaleX) || !std::isfinite(obj->m_scaleY)) return false;
    if (std::abs(obj->getRotationX() - obj->getRotationY()) > 0.05f) return false;
    return true;
}

std::vector<GameObject*> gatherSelection(EditorUI* ui) {
    std::vector<GameObject*> objects;
    if (!ui) return objects;
    if (auto* selected = ui->getSelectedObjects()) {
        objects.reserve(selected->count());
        for (auto* item : CCArrayExt<CCObject*>(selected)) {
            if (auto* obj = typeinfo_cast<GameObject*>(item)) {
                objects.push_back(obj);
            }
        }
    }
    if (objects.empty() && ui->m_selectedObject) {
        objects.push_back(ui->m_selectedObject);
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    return objects;
}

std::vector<GameObject*> gatherAllObjects(LevelEditorLayer* editor) {
    std::vector<GameObject*> objects;
    if (!editor) return objects;
    if (auto* all = editor->getAllObjects()) {
        objects.reserve(all->count());
        for (auto* item : CCArrayExt<CCObject*>(all)) {
            if (auto* obj = typeinfo_cast<GameObject*>(item)) {
                objects.push_back(obj);
            }
        }
    }
    return objects;
}

} // namespace

StretchCapability classifyObjectID(int objectID, GameObject* sampleObject) {
    return classifyObjectInfo(objectID, sampleObject).stretch;
}

CapturedScene captureEditorScene(
    LevelEditorLayer* editor,
    EditorUI* ui,
    bool selectedOnly
) {
    CapturedScene scene;
    if (!editor) return scene;

    auto selected = gatherSelection(ui);
    auto all = gatherAllObjects(editor);
    std::unordered_set<GameObject*> liveObjects;
    liveObjects.reserve(all.size());
    for (auto* obj : all) {
        if (obj) liveObjects.insert(obj);
    }

    scene.selectionCount = selected.size();
    scene.levelCount = all.size();
    scene.usedSelection = selectedOnly && !selected.empty();
    auto rawObjects = scene.usedSelection ? std::move(selected) : std::move(all);
    scene.objects.reserve(rawObjects.size());
    scene.records.reserve(rawObjects.size());

    for (std::size_t i = 0; i < rawObjects.size(); ++i) {
        GameObject* obj = rawObjects[i];
        scene.objects.emplace_back(obj);
        ObjectRecord rec;
        rec.index = i;
        if (!obj) {
            scene.records.push_back(std::move(rec));
            continue;
        }

        rec.objectID = obj->m_objectID;
        rec.isSolid = obj->m_objectType == GameObjectType::Solid &&
                      !obj->m_isNoTouch && !obj->m_isPassable;
        rec.x = obj->getPositionX();
        rec.y = obj->getPositionY();
        rec.scaleX = std::abs(obj->m_scaleX);
        rec.scaleY = std::abs(obj->m_scaleY);
        rec.rotation = normalizeRotation(obj->getRotation());
        rec.flipX = obj->isFlipX();
        rec.flipY = obj->isFlipY();

        auto const content = obj->getContentSize();
        if (obj->m_objectType == GameObjectType::Solid &&
            obj->m_width > 0.25f && obj->m_height > 0.25f) {
            rec.baseWidth = obj->m_width;
            rec.baseHeight = obj->m_height;
        } else {
            rec.baseWidth = content.width > 0.25f ? content.width : 30.f;
            rec.baseHeight = content.height > 0.25f ? content.height : 30.f;
        }

        rec.isEligible = isEligibleObject(editor, obj, liveObjects) &&
                         rec.baseWidth >= 0.5f && rec.baseHeight >= 0.5f &&
                         std::max(rec.baseWidth, rec.baseHeight) >= 4.f &&
                         std::isfinite(rec.x) && std::isfinite(rec.y);
        if (rec.isEligible) {
            auto const info = classifyObjectInfo(rec.objectID, obj);
            rec.stretch = info.stretch;
            rec.compatibilityKey = buildCompatibilityKey(
                obj, rec.rotation, info.stretch, info.rot90Sym, rec.baseWidth, rec.baseHeight
            );
        }
        scene.records.push_back(std::move(rec));
    }

    return scene;
}

Result<ApplyReport> applyOptimizationPlan(
    LevelEditorLayer* editor,
    EditorUI* ui,
    CapturedScene const& scene,
    OptimizationPlan const& plan
) {
    if (!editor || !ui || editor->m_editorUI != ui) {
        return Err("El editor ya no esta disponible.");
    }
    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sala collab esta en modo solo lectura.");
    }
    if (plan.merges.empty()) {
        return Err("No hay bloques combinables en este analisis.");
    }

    auto const allObjects = gatherAllObjects(editor);
    std::unordered_set<GameObject*> liveObjects;
    liveObjects.reserve(allObjects.size());
    for (auto* obj : allObjects) {
        if (obj) liveObjects.insert(obj);
    }

    // Validate all referenced objects are still alive in the editor before mutating anything.
    std::vector<GameObject*> toRemove;
    toRemove.reserve(plan.mergedObjectCount);
    std::string payload;
    payload.reserve(plan.merges.size() * 64);

    for (auto const& group : plan.merges) {
        if (group.keeperIndex >= scene.objects.size()) {
            return Err("Los objetos analizados cambiaron; vuelve a analizar.");
        }
        auto keeperRef = scene.objects[group.keeperIndex].lock();
        GameObject* keeper = keeperRef.data();
        if (!keeper || !liveObjects.contains(keeper)) {
            return Err("Algunos objetos ya no existen; vuelve a analizar.");
        }
        for (std::size_t idx : group.sourceIndices) {
            if (idx >= scene.objects.size()) {
                return Err("Algunos objetos ya no existen; vuelve a analizar.");
            }
            auto locked = scene.objects[idx].lock();
            if (!locked || !liveObjects.contains(locked.data())) {
                return Err("Algunos objetos ya no existen; vuelve a analizar.");
            }
            toRemove.push_back(locked.data());
        }

        std::string const baseSave(keeper->getSaveString(editor));
        if (baseSave.empty()) {
            return Err("No se pudo serializar uno de los objetos.");
        }
        // GD stores key 3 in save-space (offset from node-space by the ground height, typically -90).
        // Anchor to keeper's own save coordinates + node-space delta so merged objects spawn in place.
        float saveX = group.newX;
        float saveY = group.newY - 90.f;
        float origSaveX = 0.f;
        float origSaveY = 0.f;
        if (extractSaveXY(baseSave, origSaveX, origSaveY)) {
            saveX = origSaveX + (group.newX - keeper->getPositionX());
            saveY = origSaveY + (group.newY - keeper->getPositionY());
        }
        payload += rewriteObjectTransform(
            baseSave, saveX, saveY, group.newScaleX, group.newScaleY
        );
        payload += ';';
    }

    // Create merged objects first; if GD rejects any, roll back without touching originals.
    CCArray* created = editor->createObjectsFromString(payload, false, true);
    if (!created || created->count() != plan.merges.size()) {
        if (created) {
            for (auto* item : CCArrayExt<CCObject*>(created)) {
                if (auto* obj = typeinfo_cast<GameObject*>(item)) {
                    editor->removeObject(obj, true);
                }
            }
        }
        return Err("GD no pudo crear los objetos optimizados; no se aplicaron cambios.");
    }

    // Record one multi-delete undo step for the replaced source objects, then remove them.
    auto* removeArray = CCArray::create();
    for (auto* obj : toRemove) removeArray->addObject(obj);
    ui->deselectAll();
    ui->selectObjects(removeArray, true);
    ui->createUndoObject(UndoCommand::DeleteMulti, true);
    for (auto* obj : toRemove) {
        editor->removeObject(obj, true);
    }

    editor->updateObjectColors(created);
    editor->recreateGroups();
    editor->dirtifyTriggers();
    ui->deselectAll();
    ui->selectObjects(created, true);

    if (collab.connected()) {
        collab.sendCreatedObjects(created);
    }

    ApplyReport report;
    report.removedObjects = toRemove.size();
    report.createdObjects = created->count();
    report.netSaved = report.removedObjects > report.createdObjects
        ? report.removedObjects - report.createdObjects
        : 0;
    return Ok(report);
}

OptimizerOptions loadOptimizerOptions() {
    OptimizerOptions opts;
    auto* mod = Mod::get();
    if (!mod) return opts;
    opts.maxCollisionScale = std::clamp(
        static_cast<float>(mod->getSavedValue<int64_t>(kMaxColKey, 4)), 2.f, 8.f
    );
    opts.maxDecoScale = std::clamp(
        static_cast<float>(mod->getSavedValue<int64_t>(kMaxDecoKey, 8)), 2.f, 16.f
    );
    opts.preserveCorners = mod->getSavedValue<bool>(kPreserveCornersKey, true);
    opts.merge2DFills = mod->getSavedValue<bool>(kMerge2DKey, true);
    return opts;
}

void saveOptimizerOptions(OptimizerOptions const& options) {
    auto* mod = Mod::get();
    if (!mod) return;
    mod->setSavedValue<int64_t>(kMaxColKey, static_cast<int64_t>(std::lround(options.maxCollisionScale)));
    mod->setSavedValue<int64_t>(kMaxDecoKey, static_cast<int64_t>(std::lround(options.maxDecoScale)));
    mod->setSavedValue<bool>(kPreserveCornersKey, options.preserveCorners);
    mod->setSavedValue<bool>(kMerge2DKey, options.merge2DFills);
}

} // namespace paimon::optimizer
