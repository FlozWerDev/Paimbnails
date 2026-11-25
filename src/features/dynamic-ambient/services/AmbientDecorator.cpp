#include "AmbientDecorator.hpp"
#include "AmbientAnalyzer.hpp"
#include "AmbientNode.hpp"
#include "../../button-skins/services/ButtonSkinRuntime.hpp"
#include "../../liquid-glass/services/GlassNode.hpp"

#include <Geode/binding/AchievementBar.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCAnimatedSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/CCPartAnimSprite.hpp>
#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/binding/ColorChannelSprite.hpp>
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
#include <Geode/binding/LevelCell.hpp>
#include <Geode/binding/MenuGameLayer.hpp>
#include <Geode/binding/MultilineBitmapFont.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/binding/TableViewCell.hpp>
#include <Geode/binding/TextAlertPopup.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace paimon::ambient {
namespace {

constexpr int kPlateZ = -10000;
constexpr int kProbeZ = -3;
constexpr float kMinPanelSide = 40.f;
constexpr float kMinInsetSide = 20.f;
constexpr float kMinAssetSide = 12.f;
constexpr int kChromeDepth = 8;
constexpr int kMaxChromeNodes = 800;
constexpr int kMaxScrollNodes = 400;
constexpr unsigned kRescanFrames = 15;
constexpr double kChromeRescanSeconds = 0.45;

bool isSliceHost(CCNode* node) {
    return node && (typeinfo_cast<NineSlice*>(node) || typeinfo_cast<extension::CCScale9Sprite*>(node));
}

bool hasAmbientPlate(CCNode* node) {
    return node && node->getChildByID("paimon-ambient-node"_spr) != nullptr;
}

bool hasValidChildren(CCNode* node) {
    auto* children = node ? node->m_pChildren : nullptr;
    return children && children->data && children->data->arr && children->data->num > 0;
}

bool isGameplayOrContentNode(CCNode* node) {
    return !node ||
        typeinfo_cast<GJBaseGameLayer*>(node) != nullptr ||
        typeinfo_cast<MenuGameLayer*>(node) != nullptr ||
        typeinfo_cast<UILayer*>(node) != nullptr ||
        typeinfo_cast<EditorUI*>(node) != nullptr ||
        typeinfo_cast<ProfilePage*>(node) != nullptr ||
        typeinfo_cast<SimplePlayer*>(node) != nullptr ||
        typeinfo_cast<GJItemIcon*>(node) != nullptr ||
        typeinfo_cast<GJDifficultySprite*>(node) != nullptr ||
        typeinfo_cast<ColorChannelSprite*>(node) != nullptr ||
        typeinfo_cast<CCAnimatedSprite*>(node) != nullptr ||
        typeinfo_cast<CCPartAnimSprite*>(node) != nullptr;
}

bool isContainerSpriteNode(CCNode* node) {
    return !node ||
        typeinfo_cast<TextArea*>(node) != nullptr ||
        typeinfo_cast<MultilineBitmapFont*>(node) != nullptr ||
        typeinfo_cast<LazySprite*>(node) != nullptr;
}

bool isUserMediaOrContentId(std::string_view id) {
    if (id.empty()) return false;
    return id.find("thumbnail") != std::string_view::npos ||
           id.find("thumb") != std::string_view::npos ||
           id.find("avatar") != std::string_view::npos ||
           id.find("cover") != std::string_view::npos ||
           id.find("preview") != std::string_view::npos ||
           id.find("portrait") != std::string_view::npos;
}

bool isPanelFrameArtId(std::string_view id) {
    if (id.empty()) return false;
    return id.find("paimon-corner") != std::string_view::npos ||
           id.find("paimon-side-art") != std::string_view::npos ||
           id.find("corner") != std::string_view::npos ||
           id.find("border") != std::string_view::npos;
}

bool isCornerArtSprite(CCSprite* sprite) {
    if (!sprite) return false;
    static constexpr std::array<char const*, 4> kCornerFrames = {
        "dailyLevelCorner_001.png",
        "rewardCorner_001.png",
        "gauntletCorner_001.png",
        "GJ_sideArt_001.png",
    };
    for (char const* name : kCornerFrames) {
        if (geode::cocos::isSpriteFrameName(sprite, name)) return true;
    }
    return false;
}

bool isListBorderArtSprite(CCSprite* sprite) {
    if (!sprite) return false;
    static constexpr std::array<char const*, 8> kBorderFrames = {
        "GJ_commentTop_001.png",
        "GJ_commentTop2_001.png",
        "GJ_commentSide_001.png",
        "GJ_commentSide2_001.png",
        "GJ_table_top_001.png",
        "GJ_table_top02_001.png",
        "GJ_table_bottom_001.png",
        "GJ_table_side_001.png",
    };
    for (char const* name : kBorderFrames) {
        if (geode::cocos::isSpriteFrameName(sprite, name)) return true;
    }
    return false;
}

bool isPaimonBackgroundId(std::string_view id) {
    if (id.empty()) return false;
    return id.find("paimon-bg") != std::string_view::npos ||
           id.find("paimon-layerbg") != std::string_view::npos ||
           id.find("paimon-wallpaper") != std::string_view::npos ||
           id.find("wallpaper") != std::string_view::npos;
}

bool isSceneBackdropId(std::string_view id) {
    if (id.empty() || id == "tab-background") return false;
    return id.find("background") != std::string_view::npos ||
           id == "main-menu-bg" ||
           id == "menu-bg" ||
           id == "bg";
}

bool isBackgroundOrOverlayId(std::string_view id) {
    return isPaimonBackgroundId(id) || isSceneBackdropId(id);
}

bool isBackgroundNodeClass(CCNode* node) {
    return node && (typeinfo_cast<CCSprite*>(node) != nullptr ||
                    typeinfo_cast<CCLayerColor*>(node) != nullptr ||
                    typeinfo_cast<CCLayerGradient*>(node) != nullptr ||
                    typeinfo_cast<MenuGameLayer*>(node) != nullptr);
}

bool isUiContainerOrWidget(CCNode* node) {
    return !node ||
        typeinfo_cast<CCMenu*>(node) != nullptr ||
        typeinfo_cast<CCMenuItem*>(node) != nullptr ||
        typeinfo_cast<CCScrollLayerExt*>(node) != nullptr ||
        typeinfo_cast<GJListLayer*>(node) != nullptr ||
        typeinfo_cast<GJCommentListLayer*>(node) != nullptr ||
        typeinfo_cast<Slider*>(node) != nullptr ||
        typeinfo_cast<Scrollbar*>(node) != nullptr ||
        typeinfo_cast<TextInput*>(node) != nullptr ||
        typeinfo_cast<CCLabelBMFont*>(node) != nullptr ||
        typeinfo_cast<TextArea*>(node) != nullptr ||
        typeinfo_cast<MultilineBitmapFont*>(node) != nullptr ||
        typeinfo_cast<ButtonSprite*>(node) != nullptr ||
        typeinfo_cast<NineSlice*>(node) != nullptr ||
        typeinfo_cast<extension::CCScale9Sprite*>(node) != nullptr;
}

bool isFullScreenBackdrop(CCNode* node) {
    if (!node || isUiContainerOrWidget(node)) return false;
    if (!isBackgroundNodeClass(node) && !isBackgroundOrOverlayId(node->getID())) return false;
    auto const win = CCDirector::get()->getWinSize();
    if (!(win.width > 0.f) || !(win.height > 0.f)) return false;
    auto const sz = node->getScaledContentSize();
    float const w = std::abs(sz.width);
    float const h = std::abs(sz.height);
    if (w >= win.width * 0.78f && h >= win.height * 0.78f) return true;
    // Also skip wide ground/floor tiles sitting low in z-order.
    if (node->getZOrder() <= -4 && w >= win.width * 0.60f) return true;
    return false;
}

bool isSkinnedMenuItem(CCMenuItem* item) {
    if (!item) return false;
    if (item->getUserObject("button-skin-ui"_spr) != nullptr) return true;
    return paimon::buttonskin::ButtonSkinRuntime::hasVisibleSkin(item);
}

CCNode* sliceArtOf(CCNode* node) {
    if (auto* nine = typeinfo_cast<NineSlice*>(node)) return nine->getBatchNode();
    if (auto* scale9 = typeinfo_cast<extension::CCScale9Sprite*>(node)) return scale9->_scale9Image;
    return nullptr;
}

bool isAdditiveBlend(CCSprite* sprite) {
    if (!sprite) return false;
    auto const bf = sprite->getBlendFunc();
    return bf.dst == GL_ONE && bf.src != GL_ZERO;
}

// Samples the framebuffer right after backgrounds and strictly before foreground UI elements.
class AmbientBackdropProbe : public cocos2d::CCNode {
public:
    static AmbientBackdropProbe* create() {
        auto* node = new AmbientBackdropProbe();
        if (node && node->init()) {
            node->setID("paimon-ambient-probe"_spr);
            node->autorelease();
            return node;
        }
        delete node;
        return nullptr;
    }

    void setCanSample(bool canSample) { m_canSample = canSample; }

    void draw() override {
        if (!m_canSample || !AmbientRuntime::get().active()) return;
        CCScene const* owner = nullptr;
        for (CCNode* p = getParent(); p; p = p->getParent()) {
            if (auto* sc = typeinfo_cast<CCScene*>(p); sc && !typeinfo_cast<CCTransitionScene*>(sc)) {
                owner = sc;
                break;
            }
        }
        GLint fb = 0;
        GLint vp[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        glGetIntegerv(GL_VIEWPORT, vp);
        AmbientAnalyzer::get().maybeSampleFramebuffer(owner, fb, vp[0], vp[1], vp[2], vp[3]);
    }

private:
    bool m_canSample = true;
};

void ensureBackdropProbe(CCNode* layer) {
    if (!layer || !AmbientRuntime::get().active()) return;
    bool foundBg = false;
    int maxBgZ = -100;
    int minUiZ = 0;
    unsigned maxBgOrderAtMaxZ = 0;

    auto isLayerBackgroundNode = [](CCNode* child) {
        if (!child || !child->isVisible()) return false;
        auto const id = child->getID();
        if (typeinfo_cast<MenuGameLayer*>(child) != nullptr) return true;
        if (isBackgroundOrOverlayId(id) && !isUiContainerOrWidget(child)) return true;
        return child->getZOrder() < 0 && isFullScreenBackdrop(child);
    };

    if (hasValidChildren(layer)) {
        for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
            if (!child || !child->isVisible() || typeinfo_cast<AmbientBackdropProbe*>(child)) continue;
            int const z = child->getZOrder();
            if (isLayerBackgroundNode(child)) {
                if (!foundBg || z > maxBgZ) {
                    foundBg = true;
                    maxBgZ = z;
                    maxBgOrderAtMaxZ = child->getOrderOfArrival();
                } else if (z == maxBgZ) {
                    maxBgOrderAtMaxZ = std::max(maxBgOrderAtMaxZ, child->getOrderOfArrival());
                }
            } else {
                minUiZ = std::min(minUiZ, z);
            }
        }
    }

    bool const canSampleBeforeUi = !foundBg || maxBgZ < minUiZ;
    int probeZ = kProbeZ;
    if (foundBg) {
        probeZ = (maxBgZ + 1 < minUiZ) ? (maxBgZ + 1) : std::min(maxBgZ, minUiZ - 1);
    } else {
        probeZ = std::min(kProbeZ, minUiZ - 1);
    }

    if (auto* existing = typeinfo_cast<AmbientBackdropProbe*>(layer->getChildByID("paimon-ambient-probe"_spr))) {
        existing->setCanSample(canSampleBeforeUi);
        bool const needOrderAfterBg = foundBg && probeZ == maxBgZ && existing->getOrderOfArrival() <= maxBgOrderAtMaxZ;
        if (existing->getZOrder() != probeZ || needOrderAfterBg) {
            layer->reorderChild(existing, probeZ);
        }
        return;
    }
    if (auto* probe = AmbientBackdropProbe::create()) {
        probe->setCanSample(canSampleBeforeUi);
        layer->addChild(probe, probeZ);
    }
}

void seedBackdropFromLayer(CCNode* root, CCScene* ownerScene) {
    if (!root || !ownerScene || !hasValidChildren(root) || AmbientAnalyzer::get().hasSceneCapture(ownerScene)) return;
    for (auto* child : CCArrayExt<CCNode*>(root->getChildren())) {
        if (!child || !child->isVisible()) continue;
        if (auto* spr = typeinfo_cast<CCSprite*>(child)) {
            if ((child->getZOrder() <= -4 || isFullScreenBackdrop(child)) &&
                spr->getTexture() && !spr->getBatchNode()) {
                AmbientAnalyzer::get().seedSceneFromTexture(ownerScene, spr->getTexture(), false);
                return;
            }
        }
        auto const id = child->getID();
        if (id.find("paimon-bg-container") != std::string_view::npos ||
            id.find("paimon-layerbg-container") != std::string_view::npos) {
            if (!hasValidChildren(child)) continue;
            for (auto* sub : CCArrayExt<CCNode*>(child->getChildren())) {
                if (!sub || !sub->isVisible()) continue;
                if (auto* spr = typeinfo_cast<CCSprite*>(sub)) {
                    if (spr->getTexture() && !spr->getBatchNode()) {
                        AmbientAnalyzer::get().seedSceneFromTexture(ownerScene, spr->getTexture(), false);
                        return;
                    }
                }
            }
        }
    }
}

bool isEligibleButtonOverlaySprite(CCSprite* sprite) {
    if (!sprite || !sprite->isVisible() || sprite->getBatchNode() || !sprite->getTexture()) return false;
    if (isContainerSpriteNode(sprite) || isUserMediaOrContentId(sprite->getID())) return false;
    if (isAdditiveBlend(sprite)) return false;
    auto* tex = sprite->getTexture();
    if (tex->getPixelsWide() <= 4 || tex->getPixelsHigh() <= 4) return false;
    auto const& rect = sprite->getTextureRect();
    return std::min(std::abs(rect.size.width), std::abs(rect.size.height)) > 4.f;
}

void decorateButtonOverlays(CCNode* parent, CCNode* skipA = nullptr, CCNode* skipB = nullptr, int depth = 0) {
    if (!parent || depth > 2 || !hasValidChildren(parent)) return;
    for (auto* child : CCArrayExt<CCNode*>(parent->getChildren())) {
        if (!child || !child->isVisible() || child == skipA || child == skipB) continue;
        if (typeinfo_cast<AmbientNode*>(child) || typeinfo_cast<CCLabelBMFont*>(child) ||
            typeinfo_cast<CCClippingNode*>(child) ||
            isContainerSpriteNode(child) || isNodeIgnoredForAmbient(child)) {
            continue;
        }
        if (auto* subSpr = typeinfo_cast<CCSprite*>(child)) {
            if (isEligibleButtonOverlaySprite(subSpr)) {
                attachAmbient(subSpr, AmbientKind::ButtonOverlay, AmbientScope::Buttons);
            }
            decorateButtonOverlays(subSpr, nullptr, nullptr, depth + 1);
            continue;
        }
        if (isSliceHost(child)) {
            attachAmbient(child, AmbientKind::ButtonOverlay, AmbientScope::Buttons);
            continue;
        }
        decorateButtonOverlays(child, nullptr, nullptr, depth + 1);
    }
}

void decorateButtonLeaf(CCNode* node) {
    if (!node || isNodeIgnoredForAmbient(node)) return;
    if (auto* bs = typeinfo_cast<ButtonSprite*>(node)) {
        decorateButtonSprite(bs);
        return;
    }
    if (isSliceHost(node)) {
        attachAmbient(node, AmbientKind::Button, AmbientScope::Buttons);
        decorateButtonOverlays(node, sliceArtOf(node));
        return;
    }
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        if (sprite->getBatchNode() || !sprite->getTexture() || isAdditiveBlend(sprite)) return;
        attachAmbient(sprite, AmbientKind::Button, AmbientScope::Buttons);
        decorateButtonOverlays(sprite);
    }
}

void decorateButtonImage(CCNode* node) {
    if (!node || isNodeIgnoredForAmbient(node)) return;
    if (typeinfo_cast<ButtonSprite*>(node) || isSliceHost(node) || typeinfo_cast<CCSprite*>(node)) {
        // Primary baseplate casts shadow; child icons/badges receive shadow-free ButtonOverlay lighting.
        decorateButtonLeaf(node);
        return;
    }
    if (!hasValidChildren(node)) return;

    bool decoratedStructured = false;
    CCSprite* bestSprite = nullptr;
    float bestArea = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child || !child->isVisible() || typeinfo_cast<AmbientNode*>(child)) continue;
        if (isNodeIgnoredForAmbient(child)) continue;
        if (typeinfo_cast<ButtonSprite*>(child) || isSliceHost(child)) {
            decorateButtonLeaf(child);
            decoratedStructured = true;
            continue;
        }
        if (auto* spr = typeinfo_cast<CCSprite*>(child)) {
            if (spr->getBatchNode() || !spr->getTexture() || isAdditiveBlend(spr)) continue;
            auto const sz = spr->getScaledContentSize();
            float const area = std::abs(sz.width * sz.height);
            if (!bestSprite || area > bestArea) {
                bestSprite = spr;
                bestArea = area;
            }
        }
    }
    if (!decoratedStructured && bestSprite) {
        attachAmbient(bestSprite, AmbientKind::Button, AmbientScope::Buttons);
        decorateButtonOverlays(node, bestSprite);
    } else if (decoratedStructured) {
        decorateButtonOverlays(node);
    }
}

void decorateMenuItem(CCMenuItemSprite* item) {
    if (!item || !AmbientRuntime::get().allows(AmbientScope::Buttons)) return;
    if (isNodeIgnoredForAmbient(item)) return;
    auto* normal = item->getNormalImage();
    auto* sel = item->getSelectedImage();
    auto* dis = item->getDisabledImage();
    decorateButtonImage(normal);
    if (sel && sel != normal) {
        decorateButtonImage(sel);
    }
    if (dis && dis != normal && dis != sel) {
        decorateButtonImage(dis);
    }
    if (hasValidChildren(item)) {
        for (auto* child : CCArrayExt<CCNode*>(item->getChildren())) {
            if (!child || child == normal || child == sel || child == dis) continue;
            if (typeinfo_cast<AmbientNode*>(child) || typeinfo_cast<CCLabelBMFont*>(child)) continue;
            if (typeinfo_cast<ButtonSprite*>(child) || isSliceHost(child) || typeinfo_cast<CCSprite*>(child)) {
                decorateButtonImage(child);
            }
        }
    }
}

void decorateGeodeButton(geode::Button* btn) {
    if (!btn || !AmbientRuntime::get().allows(AmbientScope::Buttons)) return;
    if (isNodeIgnoredForAmbient(btn)) return;
    if (auto* disp = btn->getDisplayNode()) {
        decorateButtonImage(disp);
    }
}

void decorateSlider(Slider* slider) {
    if (!slider || isNodeIgnoredForAmbient(slider)) return;
    auto& runtime = AmbientRuntime::get();
    if (runtime.allows(AmbientScope::Assets) && slider->m_groove &&
        !slider->m_groove->getBatchNode() && slider->m_groove->getTexture()) {
        attachAmbient(slider->m_groove, AmbientKind::Asset, AmbientScope::Assets);
    }
    if (runtime.allows(AmbientScope::Buttons) && slider->m_touchLogic && slider->m_touchLogic->m_thumb) {
        auto* thumb = slider->m_touchLogic->m_thumb;
        for (CCNode* img : {thumb->getNormalImage(), thumb->getSelectedImage()}) {
            if (auto* spr = typeinfo_cast<CCSprite*>(img)) {
                if (!spr->getBatchNode() && spr->getTexture() && !isAdditiveBlend(spr)) {
                    attachAmbient(spr, AmbientKind::Button, AmbientScope::Buttons);
                }
            }
        }
    }
}

void decorateScrollbar(Scrollbar* scrollbar) {
    if (!scrollbar || isNodeIgnoredForAmbient(scrollbar)) return;
    auto& runtime = AmbientRuntime::get();
    if (runtime.allows(AmbientScope::Panels)) {
        if (auto* track = scrollbar->getTrack(); track && isSliceHost(track)) {
            attachAmbient(track, AmbientKind::Panel, AmbientScope::Panels);
        }
    }
    if (runtime.allows(AmbientScope::Buttons)) {
        if (auto* thumb = scrollbar->getThumb(); thumb && isSliceHost(thumb)) {
            attachAmbient(thumb, AmbientKind::Button, AmbientScope::Buttons);
        }
    }
}

bool isEligiblePanelSlice(CCNode* slice, bool topLevelLayer) {
    if (!slice || !slice->isVisible() || !isSliceHost(slice)) return false;
    auto* rgba = static_cast<CCNodeRGBA*>(slice);
    GLubyte const opacity = rgba->getOpacity();
    if (opacity < 190) {
        // Dark translucent boxes (makeInset, list wells, search fields) are recessed containers
        // sitting inside a parent panel and should stay clean rather than receiving lighting/rim.
        auto const col = rgba->getColor();
        if (std::max({col.r, col.g, col.b}) <= 85) return false;
        if (opacity < 110) return false;
    }
    auto const size = slice->getScaledContentSize();
    float const w = std::abs(size.width);
    float const h = std::abs(size.height);
    if (std::min(w, h) < kMinInsetSide) return false;
    if (topLevelLayer && isFullScreenBackdrop(slice)) return false;
    return true;
}

bool isEligiblePanelBorderSprite(CCSprite* sprite) {
    if (!sprite || !sprite->isVisible() || sprite->getBatchNode()) return false;
    if (isContainerSpriteNode(sprite)) return false;
    if (isUserMediaOrContentId(sprite->getID())) return false;
    auto* tex = sprite->getTexture();
    if (!tex || tex->getPixelsWide() <= 4 || tex->getPixelsHigh() <= 4) return false;
    auto const& texRect = sprite->getTextureRect();
    if (std::min(std::abs(texRect.size.width), std::abs(texRect.size.height)) <= 1.f) return false;
    if (isAdditiveBlend(sprite)) return false;
    if (isFullScreenBackdrop(sprite)) return false;
    auto const size = sprite->getScaledContentSize();
    float const w = std::abs(size.width);
    float const h = std::abs(size.height);
    if (std::min(w, h) < 3.f || std::max(w, h) < 12.f) return false;
    return sprite->getOpacity() >= 15;
}

bool isEligibleListColorHost(CCLayerColor* layer) {
    if (!layer || !layer->isVisible() || typeinfo_cast<CCLayerGradient*>(layer) != nullptr) return false;
    if (layer->getOpacity() < 120) return false;
    auto const col = layer->getColor();
    if (std::max({col.r, col.g, col.b}) <= 30) return false;
    auto const sz = layer->getScaledContentSize();
    return std::min(std::abs(sz.width), std::abs(sz.height)) >= 14.f;
}

void decorateListContainer(CCLayerColor* list, bool allowPanels) {
    if (!list || !allowPanels) return;
    if (isEligibleListColorHost(list)) {
        attachAmbient(list, AmbientKind::Panel, AmbientScope::Panels);
    }
    if (auto* children = list->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (auto* spr = typeinfo_cast<CCSprite*>(child); isEligiblePanelBorderSprite(spr)) {
                attachAmbient(spr, AmbientKind::Panel, AmbientScope::Panels);
            }
        }
    }
}

void decorateListBordersNode(ListBorders* borders, bool allowPanels) {
    if (!borders || !allowPanels) return;
    for (CCNode* edge : {static_cast<CCNode*>(borders->getTop()), static_cast<CCNode*>(borders->getBottom())}) {
        if (edge && edge->isVisible() && isSliceHost(edge)) {
            attachAmbient(edge, AmbientKind::Panel, AmbientScope::Panels);
        }
    }
    for (CCSprite* side : {borders->getLeft(), borders->getRight()}) {
        if (isEligiblePanelBorderSprite(side)) {
            attachAmbient(side, AmbientKind::Panel, AmbientScope::Panels);
        }
    }
}

bool isEligibleUiAssetSprite(CCSprite* sprite) {
    if (!sprite || !sprite->isVisible() || sprite->getBatchNode()) return false;
    if (isContainerSpriteNode(sprite)) return false;
    if (isUserMediaOrContentId(sprite->getID())) return false;
    auto* tex = sprite->getTexture();
    // Skip Cocos's 2x2 dummy container texture and hairline 1..4px stretched solid blocks.
    if (!tex || tex->getPixelsWide() <= 4 || tex->getPixelsHigh() <= 4) return false;
    auto const& texRect = sprite->getTextureRect();
    if (std::min(std::abs(texRect.size.width), std::abs(texRect.size.height)) <= 4.f) return false;
    if (isAdditiveBlend(sprite)) return false;
    if (isFullScreenBackdrop(sprite)) return false;
    auto const size = sprite->getScaledContentSize();
    float const w = std::abs(size.width);
    float const h = std::abs(size.height);
    if (std::min(w, h) < kMinAssetSide) return false;
    if (sprite->getOpacity() < 15) return false;
    // Skip sprites directly under a scene/layer at background z-orders.
    if (sprite->getZOrder() <= -4) {
        auto* parent = sprite->getParent();
        if (parent && (typeinfo_cast<CCScene*>(parent) || typeinfo_cast<CCLayer*>(parent))) {
            return false;
        }
    }
    return true;
}

struct UiScanOptions {
    bool inPopup = false;
    CCNode* mainBackground = nullptr;
};

void scanUiTree(CCNode* root, UiScanOptions const& opts) {
    if (!root || isNodeIgnoredForAmbient(root)) return;
    auto& runtime = AmbientRuntime::get();
    if (!runtime.active()) return;

    bool const allowButtons = runtime.allows(AmbientScope::Buttons);
    bool const allowPanels = runtime.allows(AmbientScope::Panels);
    bool const allowAssets = runtime.allows(AmbientScope::Assets);

    struct QueueItem {
        CCNode* node = nullptr;
        int depth = 0;
        bool inScroll = false;
    };
    std::vector<QueueItem> pending;
    pending.reserve(128);
    int scrollVisited = 0;

    auto pushChildren = [&](CCNode* node, int depth, bool inScroll) {
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

        if (typeinfo_cast<AmbientNode*>(node) || typeinfo_cast<AmbientBackdropProbe*>(node) ||
            typeinfo_cast<paimon::glass::GlassNode*>(node) ||
            isNodeIgnoredForAmbient(node) || isContainerSpriteNode(node) ||
            typeinfo_cast<FLAlertLayer*>(node) ||
            typeinfo_cast<GJDropDownLayer*>(node) || typeinfo_cast<DialogLayer*>(node) ||
            typeinfo_cast<CCLabelBMFont*>(node) || typeinfo_cast<CCSpriteBatchNode*>(node)) {
            continue;
        }

        if (auto* bs = typeinfo_cast<ButtonSprite*>(node)) {
            if (allowButtons) decorateButtonSprite(bs);
            continue;
        }
        if (auto* geodeBtn = typeinfo_cast<geode::Button*>(node)) {
            if (allowButtons) decorateGeodeButton(geodeBtn);
            continue;
        }
        if (auto* toggler = typeinfo_cast<CCMenuItemToggler*>(node)) {
            if (allowButtons && !isSkinnedMenuItem(toggler)) {
                if (toggler->m_onButton) decorateMenuItem(toggler->m_onButton);
                if (toggler->m_offButton) decorateMenuItem(toggler->m_offButton);
            }
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
            if (allowPanels) {
                if (auto* bg = input->getBGSprite(); bg && bg->isVisible() && bg->getOpacity() >= 15) {
                    attachAmbient(bg, AmbientKind::Panel, AmbientScope::Panels);
                }
            }
            continue;
        }
        if (auto* borders = typeinfo_cast<ListBorders*>(node)) {
            decorateListBordersNode(borders, allowPanels);
            continue;
        }
        if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(node)) {
            decorateListContainer(commentList, allowPanels);
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* listLayer = typeinfo_cast<GJListLayer*>(node)) {
            decorateListContainer(listLayer, allowPanels);
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* cell = typeinfo_cast<TableViewCell*>(node)) {
            if (opts.inPopup && allowPanels && typeinfo_cast<LevelCell*>(node) == nullptr &&
                isEligibleListColorHost(cell->m_backgroundLayer)) {
                attachAmbient(cell->m_backgroundLayer, AmbientKind::Panel, AmbientScope::Panels);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* songWidget = typeinfo_cast<CustomSongWidget*>(node)) {
            if (allowPanels && songWidget->m_bgSpr && songWidget->m_bgSpr->isVisible()) {
                attachAmbient(songWidget->m_bgSpr, AmbientKind::Panel, AmbientScope::Panels);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (isSliceHost(node)) {
            if (allowPanels && node != opts.mainBackground && !hasAmbientPlate(node) &&
                isEligiblePanelSlice(node, !opts.inPopup)) {
                attachAmbient(node, AmbientKind::Panel, AmbientScope::Panels);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }
        if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
            if (isPanelFrameArtId(sprite->getID()) || isCornerArtSprite(sprite) || isListBorderArtSprite(sprite)) {
                if (allowPanels && isEligiblePanelBorderSprite(sprite)) {
                    attachAmbient(sprite, AmbientKind::Panel, AmbientScope::Panels);
                }
            } else if (allowAssets && isEligibleUiAssetSprite(sprite)) {
                attachAmbient(sprite, AmbientKind::Asset, AmbientScope::Assets);
            }
            pushChildren(node, depth + 1, item.inScroll);
            continue;
        }

        bool const childInScroll = item.inScroll || (typeinfo_cast<CCScrollLayerExt*>(node) != nullptr);
        pushChildren(node, depth + 1, childInScroll);
    }
}

CCNode* findLargestPanelSlice(CCNode* container) {
    auto* children = container ? container->getChildren() : nullptr;
    if (!children) return nullptr;
    auto* tex07 = CCTextureCache::get() ? CCTextureCache::get()->textureForKey("GJ_square07.png") : nullptr;
    CCNode* best = nullptr;
    float bestArea = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child || !child->isVisible() || !isSliceHost(child)) continue;
        if (static_cast<CCNodeRGBA*>(child)->getOpacity() < 15) continue;
        if (tex07) {
            if (auto* batch = typeinfo_cast<CCSpriteBatchNode*>(sliceArtOf(child));
                batch && batch->getTexture() == tex07) {
                continue;
            }
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

double secondsNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::size_t layoutOf(CCNode* container) {
    if (!hasValidChildren(container)) return 0;
    auto* children = container->getChildren();
    std::size_t hash = children ? children->count() : 0;
    if (!children) return hash;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        hash = hash * 1099511628211ull + reinterpret_cast<std::uintptr_t>(child);
        if (!child || typeinfo_cast<CCLabelBMFont*>(child) ||
            typeinfo_cast<AmbientNode*>(child) || typeinfo_cast<AmbientBackdropProbe*>(child) ||
            typeinfo_cast<CCSpriteBatchNode*>(child) ||
            isGameplayOrContentNode(child)) {
            continue;
        }
        hash += child->getChildrenCount();
        if (hasValidChildren(child)) {
            for (auto* grand : CCArrayExt<CCNode*>(child->getChildren())) {
                hash = hash * 1099511628211ull + reinterpret_cast<std::uintptr_t>(grand);
                if (grand && !typeinfo_cast<CCLabelBMFont*>(grand) &&
                    !typeinfo_cast<AmbientNode*>(grand) && !typeinfo_cast<CCSpriteBatchNode*>(grand)) {
                    hash += grand->getChildrenCount();
                    if (hasValidChildren(grand)) {
                        for (auto* great : CCArrayExt<CCNode*>(grand->getChildren())) {
                            if (great && !typeinfo_cast<CCLabelBMFont*>(great) &&
                                !typeinfo_cast<AmbientNode*>(great) && !typeinfo_cast<CCSpriteBatchNode*>(great)) {
                                hash = hash * 1099511628211ull + reinterpret_cast<std::uintptr_t>(great);
                                hash += great->getChildrenCount();
                            }
                        }
                    }
                }
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
    return host->getChildByID("paimon-ambient-node"_spr) != nullptr;
}

void decorateDialogLayer(DialogLayer* dialog) {
    if (!dialog || !AmbientRuntime::get().active()) return;
    auto* main = dialog->m_mainLayer;
    if (!main) return;
    CCNode* best = nullptr;
    if (AmbientRuntime::get().allows(AmbientScope::Panels)) {
        best = findLargestPanelSlice(main);
        if (best) attachAmbient(best, AmbientKind::Panel, AmbientScope::Panels);
    }
    scanUiTree(main, {.inPopup = true, .mainBackground = best});
}

void decorateBannerNode(CCNode* node) {
    auto& runtime = AmbientRuntime::get();
    if (!node || !runtime.active()) return;
    bool const allowPanels = runtime.allows(AmbientScope::Panels);
    if (auto* bar = typeinfo_cast<AchievementBar*>(node)) {
        if (allowPanels) {
            if (bar->m_bg && bar->m_bg->isVisible()) {
                attachAmbient(bar->m_bg, AmbientKind::Panel, AmbientScope::Panels);
            }
            if (bar->m_bg2 && bar->m_bg2->isVisible()) {
                attachAmbient(bar->m_bg2, AmbientKind::Panel, AmbientScope::Panels);
            }
        }
        scanUiTree(bar, {.inPopup = true, .mainBackground = bar->m_bg});
        return;
    }
    if (auto* alert = typeinfo_cast<TextAlertPopup*>(node)) {
        if (allowPanels && alert->m_background && alert->m_background->isVisible()) {
            attachAmbient(alert->m_background, AmbientKind::Panel, AmbientScope::Panels);
        }
        scanUiTree(alert, {.inPopup = true, .mainBackground = alert->m_background});
        return;
    }
    if (auto* notif = typeinfo_cast<Notification*>(node)) {
        CCNode* bg = nullptr;
        if (auto* children = notif->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (isSliceHost(child) && child->isVisible()) {
                    bg = child;
                    if (allowPanels) {
                        attachAmbient(child, AmbientKind::Panel, AmbientScope::Panels);
                    }
                    break;
                }
            }
        }
        scanUiTree(notif, {.inPopup = true, .mainBackground = bg});
    }
}

void watchNode(CCNode* root, unsigned frame, CCScene* ownerScene = nullptr) {
    if (!root || isNodeIgnoredForAmbient(root) || typeinfo_cast<AmbientNode*>(root)) return;

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
        ensureBackdropProbe(root);
        seedBackdropFromLayer(root, ownerScene);
        decorateUiRoot(root);
        watch.host = nullptr;
    }
    watch.layout = layoutOf(tracked);
}

void watchScene(CCScene* scene, unsigned frame) {
    if (!scene) return;
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        watchScene(transition->m_pInScene, frame);
        watchScene(transition->m_pOutScene, frame);
        return;
    }
    AmbientAnalyzer::get().bindSceneSeeds(scene);
    if (!hasValidChildren(scene)) return;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        watchNode(child, frame, scene);
    }
}

} // namespace

bool isNodeIgnoredForAmbient(CCNode* node) {
    if (!node || isGameplayOrContentNode(node)) return true;

    // Never treat UI button wrappers (ButtonSprite / BasedButtonSprite / CCMenuItem)
    // as full-screen backgrounds even if their node ID contains "background" (e.g. tab-background).
    bool const isButtonNode = typeinfo_cast<ButtonSprite*>(node) != nullptr ||
        typeinfo_cast<BasedButtonSprite*>(node) != nullptr ||
        typeinfo_cast<CCMenuItem*>(node) != nullptr;
    auto const id = node->getID();
    if (isPaimonBackgroundId(id)) return true;
    // Allow popup 9-slice backgrounds (e.g. MessageThreadsPage) while still ignoring sprite/layer backdrops.
    if (!isButtonNode && !isSliceHost(node) && isSceneBackdropId(id)) return true;
    if (node->getUserObject("button-skin-ui"_spr) != nullptr) return true;
    if (auto* selfItem = typeinfo_cast<CCMenuItem*>(node); selfItem && isSkinnedMenuItem(selfItem)) return true;

    // Walk all ancestors to exclude ProfilePage, ButtonSkin UI, and gameplay subtrees at any depth.
    for (CCNode* cur = node; cur; cur = cur->getParent()) {
        if (isGameplayOrContentNode(cur)) return true;
        if (cur->getUserObject("button-skin-ui"_spr) != nullptr) return true;
        if (auto* item = typeinfo_cast<CCMenuItem*>(cur)) {
            if (isSkinnedMenuItem(item)) return true;
        }
    }
    return false;
}

AmbientNode* attachAmbient(CCNode* host, AmbientKind kind, AmbientScope scope) {
    if (!host || isNodeIgnoredForAmbient(host)) return nullptr;
    if (auto* existing = typeinfo_cast<AmbientNode*>(host->getChildByID("paimon-ambient-node"_spr))) {
        return existing;
    }
    auto* plate = AmbientNode::createForHost(host, kind, scope);
    if (!plate) return nullptr;
    host->addChild(plate, kPlateZ);
    return plate;
}

CCNode* decoratePopup(FLAlertLayer* popup) {
    auto& runtime = AmbientRuntime::get();
    if (!popup || !runtime.active() || isNodeIgnoredForAmbient(popup)) return nullptr;
    auto* main = popup->m_mainLayer;
    if (!main) return nullptr;

    CCNode* best = nullptr;
    if (runtime.allows(AmbientScope::Panels)) {
        best = findLargestPanelSlice(main);
        if (best) attachAmbient(best, AmbientKind::Panel, AmbientScope::Panels);
    }
    scanUiTree(main, {.inPopup = true, .mainBackground = best});
    return best;
}

void decorateDropDown(GJDropDownLayer* dropdown) {
    auto& runtime = AmbientRuntime::get();
    if (!dropdown || !runtime.active()) return;
    if (runtime.allows(AmbientScope::Panels) && dropdown->m_listLayer) {
        // Only decorate the list's borders/chrome, not large solid list backings if transparent.
        if (auto* children = dropdown->m_listLayer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* spr = typeinfo_cast<CCSprite*>(child);
                    spr && !spr->getBatchNode() && spr->getTexture()) {
                    attachAmbient(spr, AmbientKind::Asset, AmbientScope::Assets);
                }
            }
        }
    }
    if (dropdown->m_mainLayer) {
        scanUiTree(dropdown->m_mainLayer, {.inPopup = true, .mainBackground = dropdown->m_listLayer});
    }
}

void decorateUiRoot(CCNode* root) {
    if (!root || isNodeIgnoredForAmbient(root) || !AmbientRuntime::get().active()) return;
    scanUiTree(root, {.inPopup = false, .mainBackground = nullptr});
}

void decorateButtonSprite(ButtonSprite* button, bool immediateReveal) {
    if (!button || !AmbientRuntime::get().allows(AmbientScope::Buttons)) return;
    if (isNodeIgnoredForAmbient(button)) return;
    button->setUserObject("paimon-ambient-decorated"_spr, CCBool::create(true));
    if (auto* bg = button->m_BGSprite) {
        if (auto* plate = attachAmbient(bg, AmbientKind::Button, AmbientScope::Buttons);
            plate && immediateReveal) {
            plate->setRevealImmediate();
        }
        decorateButtonOverlays(button, bg, button->m_subBGSprite);
        if (auto* subBg = button->m_subBGSprite; isEligibleButtonOverlaySprite(subBg)) {
            if (auto* plate = attachAmbient(subBg, AmbientKind::ButtonOverlay, AmbientScope::Buttons);
                plate && immediateReveal) {
                plate->setRevealImmediate();
            }
        }
        return;
    }
    if (auto* subBg = button->m_subBGSprite; isEligibleButtonOverlaySprite(subBg)) {
        if (auto* plate = attachAmbient(subBg, AmbientKind::Button, AmbientScope::Buttons);
            plate && immediateReveal) {
            plate->setRevealImmediate();
        }
        decorateButtonOverlays(button, subBg);
    }
}

void decorateTree(CCNode* root) {
    if (!root || isNodeIgnoredForAmbient(root) || !AmbientRuntime::get().active()) return;
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
}

void decorateScene(CCScene* scene) {
    if (!scene || !AmbientRuntime::get().active()) return;
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        decorateScene(transition->m_pInScene);
        decorateScene(transition->m_pOutScene);
        return;
    }
    auto* director = CCDirector::get();
    if (director && director->getRunningScene() == scene) {
        AmbientAnalyzer::get().onSceneChanged(scene);
    } else {
        AmbientAnalyzer::get().bindSceneSeeds(scene);
    }
    if (!hasValidChildren(scene)) return;
    unsigned const frame = director ? director->getTotalFrames() : 0;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        watchNode(child, frame, scene);
    }
}

void decorateEnteredLayer(CCLayer* layer) {
    if (!layer || isNodeIgnoredForAmbient(layer) || !AmbientRuntime::get().active()) return;
    auto* parent = layer->getParent();
    auto* parentScene = typeinfo_cast<CCScene*>(parent);
    bool const isSceneOrOverlay = parent &&
        (parentScene != nullptr ||
         parent == static_cast<CCNode*>(OverlayManager::get()));
    if (isSceneOrOverlay || typeinfo_cast<FLAlertLayer*>(layer) ||
        typeinfo_cast<GJDropDownLayer*>(layer) || typeinfo_cast<DialogLayer*>(layer)) {
        if (parentScene && !typeinfo_cast<CCTransitionScene*>(parentScene)) {
            AmbientAnalyzer::get().bindSceneSeeds(parentScene);
        }
        auto* director = CCDirector::get();
        unsigned const frame = director ? director->getTotalFrames() : 0;
        watchNode(layer, frame, parentScene);
    }
}

void decorateRunningScene() {
    if (!AmbientRuntime::get().active()) return;
    auto* director = CCDirector::get();
    if (auto* scene = director ? director->getRunningScene() : nullptr) {
        decorateScene(scene);
    }
    if (auto* notifNode = director ? director->getNotificationNode() : nullptr; hasValidChildren(notifNode)) {
        for (auto* child : CCArrayExt<CCNode*>(notifNode->getChildren())) {
            if (child && !isNodeIgnoredForAmbient(child)) decorateTree(child);
        }
    }
    if (auto* overlay = OverlayManager::get(); hasValidChildren(overlay)) {
        for (auto* child : CCArrayExt<CCNode*>(overlay->getChildren())) {
            if (child && !isNodeIgnoredForAmbient(child)) decorateTree(child);
        }
    }
}

void watchPopups() {
    auto& watches = layerWatches();
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene || !AmbientRuntime::get().active()) {
        watches.clear();
        return;
    }
    AmbientAnalyzer::get().onSceneChanged(scene);
    unsigned const frame = director->getTotalFrames();
    watchScene(scene, frame);
    if (auto* next = director->getNextScene(); next && next != scene) {
        watchScene(next, frame);
    }
    if (auto* notifNode = director->getNotificationNode(); hasValidChildren(notifNode)) {
        for (auto* child : CCArrayExt<CCNode*>(notifNode->getChildren())) {
            watchNode(child, frame);
        }
    }
    if (auto* overlay = OverlayManager::get(); hasValidChildren(overlay)) {
        for (auto* child : CCArrayExt<CCNode*>(overlay->getChildren())) {
            watchNode(child, frame);
        }
    }
    std::erase_if(watches, [frame](LayerWatch const& watch) { return watch.seenFrame != frame; });
}

} // namespace paimon::ambient
