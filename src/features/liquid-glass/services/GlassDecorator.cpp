#include "GlassDecorator.hpp"
#include "GlassNode.hpp"
#include "GlassStyle.hpp"

#include <Geode/binding/AchievementBar.hpp>
#include <Geode/binding/BoomListView.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/DialogLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJDropDownLayer.hpp>
#include <Geode/binding/GJItemIcon.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/binding/TextAlertPopup.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace paimon::glass {
namespace {

// far below any slice or child the host owns, so the plate draws first.
constexpr int kPlateZ = -10000;
// smaller slice nodes inside a popup are rows and badges, not its window.
constexpr float kMinPanelSide = 40.f;
// a nested gd window smaller than this is a divider or badge; it keeps its art.
constexpr float kMinInsetSide = 20.f;
// buttons smaller than this are dots/markers; larger than this are screen catchers.
constexpr float kMinButtonSide = 10.f;
constexpr float kMaxButtonSide = 240.f;
// UI chrome depth cap and separate BFS budgets for chrome vs scroll lists so
// large tables never starve top-level menus, tabs, or utility buttons.
constexpr int kChromeDepth = 8;
constexpr int kMaxChromeNodes = 800;
constexpr int kMaxScrollNodes = 400;

bool isSliceHost(CCNode* node) {
    return node && (typeinfo_cast<NineSlice*>(node) || typeinfo_cast<extension::CCScale9Sprite*>(node));
}

bool hasGlassPlate(CCNode* node) {
    return node && node->getChildByID("paimon-glass-node"_spr) != nullptr;
}

bool hasValidChildren(CCNode* node) {
    auto* children = node ? node->m_pChildren : nullptr;
    return children && children->data && children->data->arr && children->data->num > 0;
}

// Strict gameplay & non-UI-content exclusion: Liquid Glass NEVER touches
// gameplay layers, editor canvas/HUD, animated menu gameplay or player/difficulty art.
bool isGameplayOrContentNode(CCNode* node) {
    return !node ||
        typeinfo_cast<GJBaseGameLayer*>(node) != nullptr ||
        typeinfo_cast<MenuGameLayer*>(node) != nullptr ||
        typeinfo_cast<UILayer*>(node) != nullptr ||
        typeinfo_cast<EditorUI*>(node) != nullptr ||
        typeinfo_cast<SimplePlayer*>(node) != nullptr ||
        typeinfo_cast<GJItemIcon*>(node) != nullptr ||
        typeinfo_cast<GJDifficultySprite*>(node) != nullptr;
}

// Cached lookup tables from CCTexture2D* and (CCTexture2D*, quantized rect, rotated)
// to their exact texture path and sprite frame name. Rebuilt lazily only when
// the Cocos cache sizes change or a new sheet texture appears.
struct FrameKey {
    CCTexture2D const* tex = nullptr;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool rotated = false;
    bool operator==(FrameKey const&) const = default;
};

struct FrameKeyHash {
    std::size_t operator()(FrameKey const& k) const noexcept {
        std::size_t h = reinterpret_cast<std::uintptr_t>(k.tex);
        h = h * 1099511628211ull ^ static_cast<std::size_t>(k.x);
        h = h * 1099511628211ull ^ static_cast<std::size_t>(k.y);
        h = h * 1099511628211ull ^ static_cast<std::size_t>(k.w);
        h = h * 1099511628211ull ^ static_cast<std::size_t>(k.h);
        h = h * 1099511628211ull ^ static_cast<std::size_t>(k.rotated ? 1 : 0);
        return h;
    }
};

FrameKey makeFrameKey(CCTexture2D const* tex, CCRect const& rect, bool rotated) {
    return {
        tex,
        static_cast<int>(std::lround(rect.origin.x * 4.f)),
        static_cast<int>(std::lround(rect.origin.y * 4.f)),
        static_cast<int>(std::lround(rect.size.width * 4.f)),
        static_cast<int>(std::lround(rect.size.height * 4.f)),
        rotated,
    };
}

struct AssetIndex {
    std::unordered_map<CCTexture2D const*, std::string> textures;
    std::unordered_map<FrameKey, std::string, FrameKeyHash> frames;
    std::unordered_set<CCTexture2D const*> indexedTextures;
    unsigned textureCount = 0;
    unsigned frameCount = 0;
};

AssetIndex& assetIndex() {
    static AssetIndex index;
    return index;
}

void syncAssetIndex(bool forceFrames = false) {
    auto& index = assetIndex();
    auto* texCache = CCTextureCache::get();
    auto* texDict = texCache ? texCache->m_pTextures : nullptr;
    unsigned const tCount = texDict ? texDict->count() : 0;
    bool const texChanged = texDict && tCount != index.textureCount;
    if (texChanged) {
        index.textures.clear();
        index.textures.reserve(tCount);
        for (auto [key, tex] : CCDictionaryExt<std::string_view, CCTexture2D*>(texDict)) {
            if (tex && !key.empty()) {
                index.textures[tex] = std::string(key);
            }
        }
        index.textureCount = tCount;
    }

    auto* frameCache = CCSpriteFrameCache::get();
    auto* frameDict = frameCache ? frameCache->m_pSpriteFrames : nullptr;
    unsigned const fCount = frameDict ? frameDict->count() : 0;
    if (frameDict && (forceFrames || texChanged || fCount != index.frameCount)) {
        index.frames.clear();
        index.frames.reserve(fCount);
        index.indexedTextures.clear();
        for (auto [key, frame] : CCDictionaryExt<std::string_view, CCSpriteFrame*>(frameDict)) {
            // Read m_pobTexture directly so we never trigger lazy disk loads on unrelated sheets.
            auto* tex = frame ? frame->m_pobTexture : nullptr;
            if (!tex || key.empty()) continue;
            index.indexedTextures.insert(tex);
            auto const fk = makeFrameKey(tex, frame->getRect(), frame->isRotated());
            auto it = index.frames.find(fk);
            if (it == index.frames.end()) {
                index.frames.emplace(fk, std::string(key));
            } else {
                // Prefer canonical GJ_* / button-matching names over aliases.
                std::string_view sheetName;
                if (auto tit = index.textures.find(tex); tit != index.textures.end()) {
                    sheetName = tit->second;
                }
                bool const prevMatch = classifyUiButtonFrame(it->second, sheetName).match;
                bool const nextMatch = classifyUiButtonFrame(key, sheetName).match;
                if (nextMatch && !prevMatch) it->second = std::string(key);
            }
        }
        index.frameCount = fCount;
    }
}

std::string_view textureNameOf(CCTexture2D const* tex) {
    if (!tex) return {};
    syncAssetIndex();
    auto const& map = assetIndex().textures;
    auto it = map.find(tex);
    return it != map.end() ? std::string_view(it->second) : std::string_view{};
}

std::string_view frameNameOf(CCSprite* sprite) {
    if (!sprite || !sprite->getTexture()) return {};
    auto* tex = sprite->getTexture();
    syncAssetIndex();
    auto& idx = assetIndex();
    auto const fk = makeFrameKey(tex, sprite->getTextureRect(), sprite->isTextureRectRotated());
    auto it = idx.frames.find(fk);
    if (it == idx.frames.end() && !idx.indexedTextures.contains(tex)) {
        syncAssetIndex(true);
        it = idx.frames.find(fk);
    }
    return it != idx.frames.end() ? std::string_view(it->second) : std::string_view{};
}

struct SkinFrame {
    Ref<CCSpriteFrame> frame;
    GlassColor color;
    bool neutral = false;
};

std::vector<SkinFrame>& skinFrames() {
    static std::vector<SkinFrame> frames;
    return frames;
}

bool& skinFramesComplete() {
    static bool complete = false;
    return complete;
}

std::vector<SkinFrame> const& resolvedSkins() {
    auto& frames = skinFrames();
    if (skinFramesComplete()) return frames;
    frames.clear();
    bool complete = true;
    auto* cache = CCSpriteFrameCache::get();
    for (auto const& entry : kButtonGlassColors) {
        auto* frame = cache ? cache->spriteFrameByName(entry.frame) : nullptr;
        if (!frame) {
            complete = false;
            continue;
        }
        frames.push_back({frame, entry.color, isNeutralButtonFrame(entry.frame)});
    }
    // retry on a later button if a sheet was not loaded yet.
    skinFramesComplete() = complete;
    return frames;
}

// any slice of a nine-slice is cut from its frame's area of the atlas.
CCSprite* probeSlice(CCNode* host) {
    if (auto* nine = typeinfo_cast<NineSlice*>(host)) return nine->getCenter();
    if (auto* scale9 = typeinfo_cast<extension::CCScale9Sprite*>(host)) {
        auto* batch = scale9->_scale9Image;
        auto* children = batch ? batch->getChildren() : nullptr;
        if (!children) return nullptr;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (auto* sprite = typeinfo_cast<CCSprite*>(child)) return sprite;
        }
    }
    return nullptr;
}

std::optional<GlassColor> skinColorOf(CCNode* host) {
    auto* probe = probeSlice(host);
    if (!probe || !probe->getTexture()) return std::nullopt;
    auto const rect = probe->getTextureRect();
    CCPoint const center{rect.getMidX(), rect.getMidY()};
    for (auto const& skin : resolvedSkins()) {
        auto* frame = skin.frame.data();
        if (!frame || frame->getTexture() != probe->getTexture()) continue;
        auto area = frame->getRect();
        // rotated frames occupy a transposed area of the atlas.
        if (frame->isRotated()) std::swap(area.size.width, area.size.height);
        if (area.containsPoint(center)) {
            if (skin.neutral) return std::nullopt;
            return skin.color;
        }
    }
    // Also check standalone GJ_button_01..06 texture names when not on a sheet.
    auto const texName = textureNameOf(probe->getTexture());
    auto const spec = classifyUiButtonFrame({}, texName);
    if (spec.match) return spec.color;
    return std::nullopt;
}

CCTexture2D* sliceTexture(CCNode* node) {
    if (auto* nine = typeinfo_cast<NineSlice*>(node)) {
        auto* batch = nine->getBatchNode();
        return batch ? batch->getTexture() : nullptr;
    }
    if (auto* scale9 = typeinfo_cast<extension::CCScale9Sprite*>(node)) {
        return scale9->_scale9Image ? scale9->_scale9Image->getTexture() : nullptr;
    }
    return nullptr;
}

bool isSolidGlassPanelSlice(CCNode* slice) {
    auto* tex = sliceTexture(slice);
    if (!tex) return false;
    return isGlassPanelTexture(textureNameOf(tex));
}

// Reject tiny bright color swatches (e.g. RGB previews using square02b_001.png)
// and full-screen background tint overlays.
bool isEligibleUiPanelSlice(CCNode* slice, bool topLevelLayer) {
    if (!slice || !slice->isVisible() || !isSliceHost(slice)) return false;
    auto* rgba = static_cast<CCNodeRGBA*>(slice);
    if (rgba->getOpacity() < 10) return false;
    if (!isSolidGlassPanelSlice(slice)) return false;
    auto const size = slice->getScaledContentSize();
    float const w = std::abs(size.width);
    float const h = std::abs(size.height);
    if (std::min(w, h) < kMinInsetSide) return false;
    if (topLevelLayer) {
        auto const win = CCDirector::get()->getWinSize();
        if (win.width > 0.f && win.height > 0.f &&
            w >= win.width * 0.92f && h >= win.height * 0.92f) {
            return false;
        }
    }
    // Small highly-saturated squares are color swatches, not containers.
    if (std::max(w, h) < 48.f) {
        auto const c = rgba->getColor();
        int const hi = std::max({c.r, c.g, c.b});
        int const lo = std::min({c.r, c.g, c.b});
        if (hi > 40 && static_cast<float>(hi - lo) / static_cast<float>(hi) > 0.35f) {
            return false;
        }
    }
    return true;
}

bool isCornerArt(CCSprite* sprite) {
    if (!sprite) return false;
    return isGlassCornerFrame(frameNameOf(sprite));
}

bool isListBorderArt(CCSprite* sprite) {
    if (!sprite) return false;
    return isGlassListBorderFrame(frameNameOf(sprite));
}

bool hasVisibleOverlayChild(CCNode* node) {
    if (!node) return false;
    if (auto* based = typeinfo_cast<BasedButtonSprite*>(node)) {
        if (based->getTopNode()) return true;
    }
    if (!hasValidChildren(node)) return false;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (child && child->isVisible() && !typeinfo_cast<GlassNode*>(child)) {
            return true;
        }
    }
    return false;
}

CCSize effectiveUiSize(CCNode* node) {
    if (!node) return {};
    auto scaled = node->getScaledContentSize();
    if (std::min(std::abs(scaled.width), std::abs(scaled.height)) > 0.01f) {
        return scaled;
    }
    if (std::abs(node->getScaleX()) < 0.01f || std::abs(node->getScaleY()) < 0.01f) {
        return node->getContentSize();
    }
    return scaled;
}

// Classify a CCSprite button image into a GlassButtonSpec (shape, glyph, tint).
GlassButtonSpec classifyButtonSpriteImage(CCSprite* image) {
    if (!image || image->getBatchNode() || !image->getTexture()) return {};
    if (typeinfo_cast<ButtonSprite*>(image)) return {};
    if (isGameplayOrContentNode(image)) return {};

    auto const texName = textureNameOf(image->getTexture());
    auto const frameName = frameNameOf(image);
    GlassButtonSpec spec = classifyUiButtonFrame(frameName, texName);

    // Geode BasedButtonSprite subclasses always get pure glass replacement,
    // even if their base texture uses a custom mod name.
    if (auto* based = typeinfo_cast<BasedButtonSprite*>(image)) {
        spec.match = true;
        spec.glyph = GlassGlyph::None;
        spec.requiresOverlayChild = true;
        if (typeinfo_cast<CircleButtonSprite*>(based)) {
            spec.shape = GlassShape::Circle;
        } else if (typeinfo_cast<TabButtonSprite*>(based)) {
            spec.shape = GlassShape::Pill;
            spec.spriteStyle = GlassSpriteStyle::Tab;
        } else if (spec.shape == GlassShape::SpriteMask || spec.shape == GlassShape::Auto) {
            spec.shape = GlassShape::RoundedSquare;
        }
    }

    if (!spec.match) return {};
    // Guard against ever replacing a baked-art sprite with an empty glass plate.
    if (spec.requiresOverlayChild && !hasVisibleOverlayChild(image)) {
        spec.shape = GlassShape::SpriteMask;
        spec.glyph = GlassGlyph::None;
        spec.requiresOverlayChild = false;
    }
    if (spec.mirrorArrow && image->isFlipX()) {
        spec.glyph = GlassGlyph::ChevronRight;
    }
    return spec;
}

void applySpriteSpec(GlassNode* plate, CCSprite* image, GlassButtonSpec const& spec) {
    if (!plate || !image) return;
    plate->setShape(spec.shape);
    plate->setSpriteStyle(spec.spriteStyle);
    plate->setGlyph(spec.glyph);
    plate->setSkinColor(spec.color);
    if (spec.shape == GlassShape::Circle) {
        plate->setFillFactor(0.86f);
    } else if (spec.shape == GlassShape::RoundedSquare || spec.shape == GlassShape::Pill) {
        plate->setFillFactor(0.93f);
    } else {
        plate->setFillFactor(1.f);
    }
    plate->setSpriteDynamicSync(true);
}

bool decorateButtonLeaf(CCNode* node) {
    if (!node || isGameplayOrContentNode(node)) return false;
    if (auto* bs = typeinfo_cast<ButtonSprite*>(node)) {
        decorateButtonSprite(bs);
        return true;
    }
    if (isSliceHost(node)) {
        auto* tex = sliceTexture(node);
        if (!tex) return false;
        auto const texName = textureNameOf(tex);
        if (detail::normalizedBaseName(texName) == "gj_square07") return false;
        if (!classifyUiButtonFrame({}, texName).match && !isGlassPanelTexture(texName)) return false;
        auto const size = effectiveUiSize(node);
        float const minSide = std::min(std::abs(size.width), std::abs(size.height));
        float const maxSide = std::max(std::abs(size.width), std::abs(size.height));
        if (minSide < kMinButtonSide || maxSide > kMaxButtonSide) return false;
        if (auto* plate = attachGlass(node, GlassKind::Button, GlassScope::Buttons)) {
            plate->setShape(GlassShape::Auto);
            plate->setSkinColor(skinColorOf(node));
        }
        return true;
    }
    auto* image = typeinfo_cast<CCSprite*>(node);
    if (!image || image->getBatchNode() || !image->getTexture()) return false;
    auto const size = effectiveUiSize(image);
    float const minSide = std::min(std::abs(size.width), std::abs(size.height));
    float const maxSide = std::max(std::abs(size.width), std::abs(size.height));
    if (minSide < kMinButtonSide || maxSide > kMaxButtonSide) return false;

    auto const spec = classifyButtonSpriteImage(image);
    if (!spec.match) return false;
    if (auto* plate = attachGlass(image, GlassKind::Button, GlassScope::Buttons)) {
        applySpriteSpec(plate, image, spec);
    }
    return true;
}

void decorateSpriteButtonImage(CCNode* node) {
    if (!node || isGameplayOrContentNode(node)) return;
    if (decorateButtonLeaf(node)) return;
    // Some mod buttons wrap a ButtonSprite/NineSlice/CCSprite inside a plain CCNode.
    if (hasValidChildren(node)) {
        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (child && child->isVisible() && decorateButtonLeaf(child)) break;
        }
    }
}

void decorateMenuItem(CCMenuItemSprite* item) {
    if (!item || !GlassRuntime::get().allows(GlassScope::Buttons)) return;
    decorateSpriteButtonImage(item->getNormalImage());
    if (auto* sel = item->getSelectedImage(); sel && sel != item->getNormalImage()) {
        decorateSpriteButtonImage(sel);
    }
    if (auto* dis = item->getDisabledImage();
        dis && dis != item->getNormalImage() && dis != item->getSelectedImage()) {
        decorateSpriteButtonImage(dis);
    }
}

void decorateListLayer(CCLayerColor* list, GlassKind kind) {
    if (!list) return;
    attachGlass(list, kind, GlassScope::Popups);
    // Keep GD's table/comment border sprites at their own top Z-order so opaque
    // scrolled rows stay cleanly framed, replacing only their wood/flat chrome
    // with frosted contour-matched Liquid Glass.
    if (auto* children = list->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            auto* sprite = typeinfo_cast<CCSprite*>(child);
            if (!sprite || sprite->getBatchNode() || !sprite->getTexture() || !isListBorderArt(sprite)) {
                continue;
            }
            if (auto* borderPlate = attachGlass(sprite, GlassKind::Button, GlassScope::Popups)) {
                borderPlate->setShape(GlassShape::SpriteMask);
                borderPlate->setSpriteStyle(GlassSpriteStyle::Chrome);
                borderPlate->setGlyph(GlassGlyph::None);
                borderPlate->setSkinColor(std::nullopt);
                borderPlate->setFillFactor(1.f);
            }
        }
    }
}

void decorateListBorders(ListBorders* borders) {
    auto* plate = attachGlass(borders, GlassKind::Well, GlassScope::Popups);
    if (!plate) return;
    std::vector<CCNode*> art;
    for (CCNode* node : {static_cast<CCNode*>(borders->getTop()), static_cast<CCNode*>(borders->getBottom()),
                         static_cast<CCNode*>(borders->getLeft()), static_cast<CCNode*>(borders->getRight())}) {
        if (node) art.push_back(node);
    }
    plate->setCompanionArt(art);
}

void decorateSlider(Slider* slider) {
    if (!slider) return;
    auto& runtime = GlassRuntime::get();
    if (runtime.allows(GlassScope::Popups) && slider->m_groove) {
        auto const texName = textureNameOf(slider->m_groove->getTexture());
        if (isSliderGrooveTexture(texName)) {
            if (auto* plate = attachGlass(slider->m_groove, GlassKind::Well, GlassScope::Popups)) {
                plate->setShape(GlassShape::Pill);
                plate->setFillFactor(0.96f);
            }
        }
    }
    if (runtime.allows(GlassScope::Buttons) && slider->m_touchLogic && slider->m_touchLogic->m_thumb) {
        auto* thumb = slider->m_touchLogic->m_thumb;
        for (CCNode* img : {thumb->getNormalImage(), thumb->getSelectedImage()}) {
            auto* spr = typeinfo_cast<CCSprite*>(img);
            if (!spr || spr->getBatchNode() || !spr->getTexture()) continue;
            auto const texName = textureNameOf(spr->getTexture());
            // Only glassify vanilla sliderthumb sprites; leave CustomSlider icons untouched.
            if (!isSliderThumbTexture(texName)) continue;
            auto const spec = classifyUiButtonFrame({}, texName);
            if (spec.match) {
                if (auto* plate = attachGlass(spr, GlassKind::Button, GlassScope::Buttons)) {
                    applySpriteSpec(plate, spr, spec);
                    plate->setFillFactor(0.84f);
                }
            }
        }
    }
}

void decorateScrollbar(Scrollbar* scrollbar) {
    if (!scrollbar) return;
    auto& runtime = GlassRuntime::get();
    if (runtime.allows(GlassScope::Popups)) {
        if (auto* track = scrollbar->getTrack()) {
            if (auto* plate = attachGlass(track, GlassKind::Well, GlassScope::Popups)) {
                plate->setShape(GlassShape::Pill);
            }
        }
    }
    if (runtime.allows(GlassScope::Buttons)) {
        if (auto* thumb = scrollbar->getThumb()) {
            if (auto* plate = attachGlass(thumb, GlassKind::Button, GlassScope::Buttons)) {
                plate->setShape(GlassShape::Pill);
            }
        }
    }
}

// the slice art of a slice host: never scanned.
CCNode* sliceArtOf(CCNode* node) {
    if (auto* nine = typeinfo_cast<NineSlice*>(node)) return nine->getBatchNode();
    if (auto* scale9 = typeinfo_cast<extension::CCScale9Sprite*>(node)) return scale9->_scale9Image;
    return nullptr;
}

// Scan a UI container (popup m_mainLayer, dropdown, or full-screen UI layer)
// and attach Liquid Glass to its panels, wells, lists, inputs, sliders,
// scrollbars and UI buttons. Gameplay subtrees are strictly skipped.
struct UiScanOptions {
    bool inPopup = false;          // true inside FLAlertLayer / GJDropDownLayer
    bool hideCorners = false;      // true when the owning popup panel is glass
    CCNode* mainBackground = nullptr;
    std::vector<CCNode*>* outCorners = nullptr;
};

void scanUiTree(CCNode* root, UiScanOptions const& opts) {
    if (!root || isGameplayOrContentNode(root)) return;
    auto& runtime = GlassRuntime::get();
    if (!runtime.active()) return;

    bool const allowPanels = runtime.allows(GlassScope::Popups);
    bool const allowButtons = runtime.allows(GlassScope::Buttons);
    bool const decorateNestedPanels = allowPanels && (!opts.inPopup || opts.hideCorners);
    GlassKind const nestedContainerKind = opts.inPopup ? GlassKind::Well : GlassKind::Panel;

    struct QueueItem {
        CCNode* node = nullptr;
        int depth = 0;
        bool inScroll = false;
    };
    std::vector<QueueItem> pending;
    pending.reserve(128);
    int scrollVisited = 0;
    auto pushChildren = [&pending, &scrollVisited](CCNode* node, int depth, bool inScroll) {
        auto* children = node->getChildren();
        if (!children || depth > kChromeDepth) return;
        if (inScroll && scrollVisited >= kMaxScrollNodes) return;
        auto* art = sliceArtOf(node);
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (child && child != art) pending.push_back({child, depth, inScroll});
        }
    };
    pushChildren(root, 1, false);

    int chromeVisited = 0;
    for (std::size_t head = 0; head < pending.size(); ++head) {
        auto const item = pending[head];
        CCNode* node = item.node;
        int const depth = item.depth;
        if (!node) continue;
        if (item.inScroll) {
            if (scrollVisited >= kMaxScrollNodes) continue;
            ++scrollVisited;
        } else {
            if (chromeVisited >= kMaxChromeNodes) continue;
            ++chromeVisited;
        }

        // Skip plates, gameplay layers, nested overlays (watched separately),
        // bitmap labels and sprite batches.
        if (typeinfo_cast<GlassNode*>(node) || isGameplayOrContentNode(node) ||
            typeinfo_cast<FLAlertLayer*>(node) || typeinfo_cast<GJDropDownLayer*>(node) ||
            typeinfo_cast<DialogLayer*>(node) || typeinfo_cast<CCLabelBMFont*>(node) ||
            typeinfo_cast<CCSpriteBatchNode*>(node)) {
            continue;
        }

        if (auto* bs = typeinfo_cast<ButtonSprite*>(node)) {
            if (allowButtons) decorateButtonSprite(bs);
            continue;
        }
        if (auto* itemSprite = typeinfo_cast<CCMenuItemSprite*>(node)) {
            if (allowButtons) decorateMenuItem(itemSprite);
            continue;
        }
        if (auto* slider = typeinfo_cast<Slider*>(node)) {
            decorateSlider(slider);
            continue;
        }
        if (auto* scrollbar = typeinfo_cast<Scrollbar*>(node)) {
            decorateScrollbar(scrollbar);
            continue;
        }
        if (auto* input = typeinfo_cast<TextInput*>(node)) {
            if (decorateNestedPanels) {
                if (auto* bg = input->getBGSprite(); bg && bg->isVisible() && bg->getOpacity() >= 10) {
                    attachGlass(bg, nestedContainerKind, GlassScope::Popups);
                }
            }
            continue;
        }
        if (auto* borders = typeinfo_cast<ListBorders*>(node)) {
            if (decorateNestedPanels) decorateListBorders(borders);
            continue;
        }
        if (auto* songWidget = typeinfo_cast<CustomSongWidget*>(node)) {
            if (decorateNestedPanels && songWidget->m_bgSpr && songWidget->m_bgSpr->isVisible()) {
                attachGlass(songWidget->m_bgSpr, nestedContainerKind, GlassScope::Popups);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(node)) {
            if (decorateNestedPanels) decorateListLayer(commentList, nestedContainerKind);
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* listLayer = typeinfo_cast<GJListLayer*>(node)) {
            if (decorateNestedPanels) decorateListLayer(listLayer, nestedContainerKind);
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (isSliceHost(node)) {
            if (decorateNestedPanels && node != opts.mainBackground && !hasGlassPlate(node) &&
                isEligibleUiPanelSlice(node, !opts.inPopup)) {
                attachGlass(node, nestedContainerKind, GlassScope::Popups);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
            if (opts.outCorners && isCornerArt(sprite)) {
                opts.outCorners->push_back(sprite);
                continue;
            }
        }
        bool const childInScroll = item.inScroll || (typeinfo_cast<CCScrollLayerExt*>(node) != nullptr);
        pushChildren(node, depth + 1, childInScroll);
    }
}

CCNode* findLargestPanelSlice(CCNode* container) {
    auto* children = container ? container->getChildren() : nullptr;
    if (!children) return nullptr;
    CCNode* best = nullptr;
    float bestArea = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child || !child->isVisible() || !isSliceHost(child)) continue;
        // both slice types are CCNodeRGBA; a transparent one is not a window.
        if (static_cast<CCNodeRGBA*>(child)->getOpacity() < 10) continue;
        // Skip hollow border overlays (GJ_square07.png).
        if (auto* tex = sliceTexture(child)) {
            auto const name = textureNameOf(tex);
            if (detail::normalizedBaseName(name) == "gj_square07") continue;
        }
        auto const size = child->getScaledContentSize();
        float const w = std::abs(size.width);
        float const h = std::abs(size.height);
        if (std::min(w, h) < kMinPanelSide) continue;
        if (w * h > bestArea) {
            best = child;
            bestArea = w * h;
        }
    }
    return best;
}

void decorateDialogLayer(DialogLayer* dialog) {
    if (!dialog || !GlassRuntime::get().active()) return;
    auto* main = dialog->m_mainLayer;
    if (!main) return;
    CCNode* best = nullptr;
    if (GlassRuntime::get().allows(GlassScope::Popups)) {
        best = findLargestPanelSlice(main);
        if (best) attachGlass(best, GlassKind::Panel, GlassScope::Popups);
    }
    scanUiTree(main, {.inPopup = true, .hideCorners = best != nullptr, .mainBackground = best});
}

void decorateBannerNode(CCNode* node) {
    if (!node || !GlassRuntime::get().allows(GlassScope::Popups)) return;
    if (auto* bar = typeinfo_cast<AchievementBar*>(node)) {
        if (bar->m_bg && bar->m_bg->isVisible()) {
            attachGlass(bar->m_bg, GlassKind::Panel, GlassScope::Popups);
        }
        return;
    }
    if (auto* alert = typeinfo_cast<TextAlertPopup*>(node)) {
        if (alert->m_background && alert->m_background->isVisible()) {
            attachGlass(alert->m_background, GlassKind::Panel, GlassScope::Popups);
        }
        return;
    }
    if (auto* notif = typeinfo_cast<Notification*>(node)) {
        if (auto* children = notif->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (isSliceHost(child) && child->isVisible()) {
                    attachGlass(child, GlassKind::Panel, GlassScope::Popups);
                    break;
                }
            }
        }
    }
}

// What the watcher last saw of one scene child or overlay root.
struct LayerWatch {
    CCNode const* root = nullptr;
    CCNode const* mainLayer = nullptr;
    CCNode* host = nullptr;
    std::size_t layout = 0;
    unsigned seenFrame = 0;
    unsigned scanFrame = 0;
    double scanTime = 0.0;
    bool isPopup = false;
};

// popups without a usable background (yet) are rescanned at this pace.
constexpr unsigned kRescanFrames = 30;
// decorated UI layers/popups are rescanned this often for dynamic nested chrome
// (e.g. a list rebuilt after network load, garage page switch, etc.).
constexpr double kChromeRescanSeconds = 0.15;

double secondsNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// identity and child counts of a root's children (up to depth 2): a layer,
// popup, or list that swaps tabs, loads cells, or adds buttons changes it.
// label child counts follow their text, so labels count by identity only.
std::size_t layoutOf(CCNode* container) {
    if (!hasValidChildren(container)) return 0;
    auto* children = container->getChildren();
    std::size_t hash = children ? children->count() : 0;
    if (!children) return hash;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        hash = hash * 1099511628211ull + reinterpret_cast<std::uintptr_t>(child);
        if (!child || typeinfo_cast<CCLabelBMFont*>(child) ||
            typeinfo_cast<GlassNode*>(child) || typeinfo_cast<CCSpriteBatchNode*>(child) ||
            isGameplayOrContentNode(child)) {
            continue;
        }
        hash += child->getChildrenCount();
        if (hasValidChildren(child)) {
            for (auto* grand : CCArrayExt<CCNode*>(child->getChildren())) {
                hash = hash * 1099511628211ull + reinterpret_cast<std::uintptr_t>(grand);
                if (grand && !typeinfo_cast<CCLabelBMFont*>(grand) &&
                    !typeinfo_cast<GlassNode*>(grand) && !typeinfo_cast<CCSpriteBatchNode*>(grand)) {
                    hash += grand->getChildrenCount();
                }
            }
        }
        if (auto* list = typeinfo_cast<GJListLayer*>(child)) {
            if (list->m_listView && hasValidChildren(list->m_listView)) {
                hash = hash * 1099511628211ull + list->m_listView->getChildrenCount();
            }
        } else if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
            if (commentList->m_list && hasValidChildren(commentList->m_list)) {
                hash = hash * 1099511628211ull + commentList->m_list->getChildrenCount();
            }
        }
    }
    return hash;
}

std::vector<LayerWatch>& layerWatches() {
    static std::vector<LayerWatch> watches;
    return watches;
}

bool plateIntact(CCNode* main, CCNode* host) {
    auto* children = main ? main->getChildren() : nullptr;
    if (!host || !children || !children->containsObject(host)) return false;
    return host->getChildByID("paimon-glass-node"_spr) != nullptr;
}

void watchNode(CCNode* root, unsigned frame) {
    if (!root || isGameplayOrContentNode(root) || typeinfo_cast<GlassNode*>(root)) return;

    auto* popup = typeinfo_cast<FLAlertLayer*>(root);
    auto* dropdown = popup ? nullptr : typeinfo_cast<GJDropDownLayer*>(root);
    auto* dialog = (popup || dropdown) ? nullptr : typeinfo_cast<DialogLayer*>(root);

    CCNode* tracked = root;
    if (popup) tracked = popup->m_mainLayer;
    else if (dropdown) tracked = dropdown->m_mainLayer;
    else if (dialog) tracked = dialog->m_mainLayer;
    if (!tracked) return;

    auto& watches = layerWatches();
    auto it = std::ranges::find(watches, static_cast<CCNode const*>(root), &LayerWatch::root);
    if (it == watches.end()) {
        watches.push_back({.root = root, .isPopup = popup != nullptr});
        it = std::prev(watches.end());
    }
    auto& watch = *it;
    watch.seenFrame = frame;

    std::size_t const curLayout = layoutOf(tracked);
    bool const unchanged = watch.mainLayer == tracked && watch.layout == curLayout;
    if (unchanged) {
        if (watch.isPopup && !watch.host) {
            if (frame - watch.scanFrame < kRescanFrames) return;
        } else {
            bool const intact = !watch.host || plateIntact(tracked, watch.host);
            if (intact && secondsNow() - watch.scanTime < kChromeRescanSeconds) return;
        }
    }

    watch.mainLayer = tracked;
    watch.scanFrame = frame;
    watch.scanTime = secondsNow();
    if (popup) {
        watch.host = decoratePopup(popup);
    } else if (dropdown) {
        decorateDropDown(dropdown);
        watch.host = nullptr;
    } else if (dialog) {
        decorateDialogLayer(dialog);
        watch.host = nullptr;
    } else if (typeinfo_cast<AchievementBar*>(root) || typeinfo_cast<TextAlertPopup*>(root) ||
               typeinfo_cast<Notification*>(root)) {
        decorateBannerNode(root);
        watch.host = nullptr;
    } else {
        decorateUiRoot(root);
        watch.host = nullptr;
    }
    // plates and wells are children too: compare against the decorated layout.
    watch.layout = layoutOf(tracked);
}

void watchScene(CCScene* scene, unsigned frame) {
    if (!scene) return;
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        watchScene(transition->m_pInScene, frame);
        watchScene(transition->m_pOutScene, frame);
        return;
    }
    if (!hasValidChildren(scene)) return;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        watchNode(child, frame);
    }
}

} // namespace

GlassNode* attachGlass(CCNode* host, GlassKind kind, GlassScope scope) {
    if (!host) return nullptr;
    if (auto* existing = typeinfo_cast<GlassNode*>(host->getChildByID("paimon-glass-node"_spr))) {
        return existing;
    }
    auto* plate = GlassNode::createForHost(host, kind, scope);
    if (!plate) return nullptr;
    host->addChild(plate, kPlateZ);
    return plate;
}

void refreshSpritePlate(GlassNode* plate, CCSprite* sprite) {
    if (!plate || !sprite) return;
    auto const spec = classifyButtonSpriteImage(sprite);
    if (spec.match) applySpriteSpec(plate, sprite, spec);
}

CCNode* decoratePopup(FLAlertLayer* popup) {
    auto& runtime = GlassRuntime::get();
    if (!popup || !runtime.active()) return nullptr;
    auto* main = popup->m_mainLayer;
    if (!main) return nullptr;

    CCNode* best = nullptr;
    GlassNode* panel = nullptr;
    if (runtime.allows(GlassScope::Popups)) {
        best = findLargestPanelSlice(main);
        if (best) panel = attachGlass(best, GlassKind::Panel, GlassScope::Popups);
    }

    std::vector<CCNode*> corners;
    scanUiTree(main, {
        .inPopup = true,
        .hideCorners = panel != nullptr,
        .mainBackground = best,
        .outCorners = panel ? &corners : nullptr,
    });
    if (panel) panel->setCompanionArt(corners);
    return panel ? best : nullptr;
}

void decorateDropDown(GJDropDownLayer* dropdown) {
    auto& runtime = GlassRuntime::get();
    if (!dropdown || !runtime.active()) return;
    bool hasPanel = false;
    if (runtime.allows(GlassScope::Popups) && dropdown->m_listLayer) {
        decorateListLayer(dropdown->m_listLayer, GlassKind::Panel);
        hasPanel = true;
    }
    if (dropdown->m_mainLayer) {
        scanUiTree(dropdown->m_mainLayer, {
            .inPopup = true,
            .hideCorners = hasPanel,
            .mainBackground = dropdown->m_listLayer,
        });
    }
}

void decorateUiRoot(CCNode* root) {
    if (!root || isGameplayOrContentNode(root) || !GlassRuntime::get().active()) return;
    scanUiTree(root, {.inPopup = false, .hideCorners = false, .mainBackground = nullptr});
}

void decorateButtonSprite(ButtonSprite* button) {
    if (!button || !GlassRuntime::get().allows(GlassScope::Buttons)) return;
    if (auto* background = button->m_BGSprite) {
        if (auto* plate = attachGlass(background, GlassKind::Button, GlassScope::Buttons)) {
            plate->setSkinColor(skinColorOf(background));
        }
        return;
    }
    if (auto* subBg = button->m_subBGSprite; subBg && !subBg->getBatchNode() && subBg->getTexture()) {
        auto const texName = textureNameOf(subBg->getTexture());
        auto const frameName = frameNameOf(subBg);
        auto spec = classifyUiButtonFrame(frameName, texName);
        if (!spec.match) {
            spec = {true, GlassShape::Auto, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, true};
        }
        bool const hasOverlay = (button->m_subSprite && button->m_subSprite->isVisible()) ||
            (button->m_label && button->m_label->isVisible()) ||
            hasVisibleOverlayChild(subBg);
        if (spec.requiresOverlayChild && !hasOverlay) {
            spec.shape = GlassShape::SpriteMask;
            spec.glyph = GlassGlyph::None;
            spec.requiresOverlayChild = false;
        }
        if (auto* plate = attachGlass(subBg, GlassKind::Button, GlassScope::Buttons)) {
            applySpriteSpec(plate, subBg, spec);
        }
    }
}

void decorateTree(CCNode* root) {
    if (!root || isGameplayOrContentNode(root) || !GlassRuntime::get().active()) return;
    if (auto* popup = typeinfo_cast<FLAlertLayer*>(root)) {
        decoratePopup(popup);
        return;
    }
    if (auto* dropdown = typeinfo_cast<GJDropDownLayer*>(root)) {
        decorateDropDown(dropdown);
        return;
    }
    if (auto* dialog = typeinfo_cast<DialogLayer*>(root)) {
        decorateDialogLayer(dialog);
        return;
    }
    if (typeinfo_cast<AchievementBar*>(root) || typeinfo_cast<TextAlertPopup*>(root) ||
        typeinfo_cast<Notification*>(root)) {
        decorateBannerNode(root);
        return;
    }
    decorateUiRoot(root);
    if (auto* children = root->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child || isGameplayOrContentNode(child)) continue;
            if (typeinfo_cast<FLAlertLayer*>(child) || typeinfo_cast<GJDropDownLayer*>(child) ||
                typeinfo_cast<DialogLayer*>(child) || typeinfo_cast<AchievementBar*>(child) ||
                typeinfo_cast<TextAlertPopup*>(child) || typeinfo_cast<Notification*>(child)) {
                decorateTree(child);
            }
        }
    }
}

void decorateScene(CCScene* scene) {
    if (!scene || !GlassRuntime::get().active()) return;
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        decorateScene(transition->m_pInScene);
        decorateScene(transition->m_pOutScene);
        return;
    }
    if (!hasValidChildren(scene)) return;
    auto* director = CCDirector::get();
    unsigned const frame = director ? director->getTotalFrames() : 0;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        watchNode(child, frame);
    }
}

void decorateEnteredLayer(CCLayer* layer) {
    if (!layer || isGameplayOrContentNode(layer) || !GlassRuntime::get().active()) return;
    if (typeinfo_cast<GJListLayer*>(layer) || typeinfo_cast<GJCommentListLayer*>(layer)) {
        bool inPopup = false;
        for (CCNode* p = layer->getParent(); p; p = p->getParent()) {
            if (isGameplayOrContentNode(p)) return;
            if (typeinfo_cast<FLAlertLayer*>(p) || typeinfo_cast<GJDropDownLayer*>(p) ||
                typeinfo_cast<DialogLayer*>(p)) {
                inPopup = true;
                break;
            }
        }
        if (GlassRuntime::get().allows(GlassScope::Popups)) {
            decorateListLayer(static_cast<CCLayerColor*>(layer),
                              inPopup ? GlassKind::Well : GlassKind::Panel);
        }
        scanUiTree(layer, {.inPopup = inPopup, .hideCorners = inPopup, .mainBackground = layer});
        return;
    }
    auto* parent = layer->getParent();
    bool const isSceneOrOverlay = parent &&
        (typeinfo_cast<CCScene*>(parent) != nullptr ||
         parent == static_cast<CCNode*>(OverlayManager::get()));
    if (isSceneOrOverlay || typeinfo_cast<FLAlertLayer*>(layer) ||
        typeinfo_cast<GJDropDownLayer*>(layer) || typeinfo_cast<DialogLayer*>(layer)) {
        auto* director = CCDirector::get();
        unsigned const frame = director ? director->getTotalFrames() : 0;
        watchNode(layer, frame);
    }
}

void decorateRunningScene() {
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        decorateScene(scene);
    }
    if (auto* overlay = OverlayManager::get(); hasValidChildren(overlay)) {
        for (auto* child : CCArrayExt<CCNode*>(overlay->getChildren())) {
            if (child && !isGameplayOrContentNode(child)) decorateTree(child);
        }
    }
}

void watchPopups() {
    auto& watches = layerWatches();
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene || !GlassRuntime::get().active()) {
        watches.clear();
        return;
    }

    unsigned const frame = director->getTotalFrames();
    watchScene(scene, frame);
    if (auto* next = director->getNextScene(); next && next != scene) {
        watchScene(next, frame);
    }
    if (auto* overlay = OverlayManager::get(); hasValidChildren(overlay)) {
        for (auto* child : CCArrayExt<CCNode*>(overlay->getChildren())) {
            watchNode(child, frame);
        }
    }
    std::erase_if(watches, [frame](LayerWatch const& watch) { return watch.seenFrame != frame; });
}

void invalidateSkinCache() {
    skinFrames().clear();
    skinFramesComplete() = false;
    auto& index = assetIndex();
    index.textures.clear();
    index.frames.clear();
    index.indexedTextures.clear();
    index.textureCount = 0;
    index.frameCount = 0;
}

} // namespace paimon::glass
