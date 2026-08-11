#include "CollabOverlay.hpp"

#include "CollabEmotes.hpp"
#include "CollabManager.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"
#include "../editor-suite/EditorModule.hpp"
#include "../editor-suite/EditorEvents.hpp"
#include "../../utils/ImageLoadHelper.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr float kTagBaseScale = 0.45f;
constexpr size_t kMaxConcurrentFlashes = 48;
constexpr float kFlashLife = 0.45f;

constexpr int   kMaxToasts      = 3;
constexpr float kToastHold      = 4.0f;
constexpr float kToastEnter     = 0.4f;
constexpr float kToastExit      = 0.3f;
constexpr float kToastGap       = 6.f;
constexpr float kToastTopMargin = 42.f;
constexpr float kToastRightMargin = 8.f;
constexpr int   kToastMoveTag   = 71;
constexpr float kChipHeight  = 26.f;
constexpr float kChipGap     = 8.f;
constexpr int   kChipMoveTag = 72;

constexpr float kHudLeft       = 56.f;
constexpr float kHudToolbarGap = 22.f;
constexpr float kHudRowGap     = 28.f;
constexpr float kHudStatusX    = 114.f;
constexpr float kHudStatusH    = 22.f;
constexpr float kDefaultToolbarHeight = 92.f;

constexpr size_t kTrailMaxPts = 14;
constexpr float kTrailMinDist = 18.f;
constexpr float kTrailDrainEvery = 0.2f;
constexpr float kHeatRedrawEvery = 0.35f;
constexpr int kMaxRemoteCursorDimension = 512;

// CCDrawNode blends premultiplied (CC_BLEND_SRC is GL_ONE), so full-brightness
// rgb with a low alpha reads as additive glow instead of a soft tint — that is
// what turned the presence rects into solid neon. Scale rgb by alpha.
ccColor4F drawColor(ccColor3B c, float alpha) {
    float a = std::clamp(alpha, 0.f, 1.f);
    return {c.r / 255.f * a, c.g / 255.f * a, c.b / 255.f * a, a};
}

constexpr ccColor4F kNoFill{0.f, 0.f, 0.f, 0.f};

float outlineWidth(float zoom, float px) {
    return std::clamp(px / (zoom > 0.f ? zoom : 1.f), 0.3f, 14.f);
}

bool customCursorsEnabled() {
    return paimon::editor::featureEnabled("collab-custom-cursors");
}

std::vector<uint8_t> decodeBase64(std::string const& input) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    if (input.empty() || input.size() > kMaxCursorDataLength) return out;
    out.reserve(input.size() * 3 / 4);
    uint32_t value = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        auto* found = std::find(std::begin(kAlphabet), std::end(kAlphabet) - 1, static_cast<char>(c));
        if (found == std::end(kAlphabet) - 1) return {};
        value = (value << 6) | static_cast<uint32_t>(found - kAlphabet);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
            bits -= 8;
            if (out.size() > kMaxCursorAssetBytes) return {};
        }
    }
    return out;
}

CCSprite* createRemoteCursor(PeerAppearance const& appearance) {
    auto data = decodeBase64(appearance.cursorData);
    if (data.empty()) return nullptr;

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data.data(), static_cast<int>(data.size()),
                               &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > kMaxRemoteCursorDimension ||
        height > kMaxRemoteCursorDimension) return nullptr;

    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data.data(), data.size(), false);
    if (!loaded.success || !loaded.texture) return nullptr;
    auto* sprite = CCSprite::createWithTexture(loaded.texture);
    loaded.texture->release();
    if (!sprite) return nullptr;
    float targetSize = std::max(4.f, 100.f * appearance.cursorScale);
    geode::cocos::limitNodeSize(sprite, {targetSize, targetSize}, 999.f, 0.0001f);
    sprite->setAnchorPoint({0.f, 1.f});
    sprite->setOpacity(static_cast<GLubyte>(appearance.cursorOpacity));
    return sprite;
}

IconType peerIconTypeLocal(int raw) {
    switch (raw) {
        case static_cast<int>(IconType::Ship): return IconType::Ship;
        case static_cast<int>(IconType::Ball): return IconType::Ball;
        case static_cast<int>(IconType::Ufo): return IconType::Ufo;
        case static_cast<int>(IconType::Wave): return IconType::Wave;
        case static_cast<int>(IconType::Robot): return IconType::Robot;
        case static_cast<int>(IconType::Spider): return IconType::Spider;
        case static_cast<int>(IconType::Swing): return IconType::Swing;
        case static_cast<int>(IconType::Jetpack): return IconType::Jetpack;
        default: return IconType::Cube;
    }
}

// Fades every RGBA-capable node in the tree; plain container nodes in this
// cocos fork don't cascade opacity, so each descendant animates itself.
void fadeOutTree(CCNode* node, float duration) {
    if (!node) return;
    if (dynamic_cast<CCRGBAProtocol*>(node)) {
         node->stopAllActions();
        node->runAction(CCFadeOut::create(duration));
    }
    if (auto* kids = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(kids)) fadeOutTree(child, duration);
    }
}

void fadeInTree(CCNode* node, float duration) {
    if (!node) return;
    if (auto* rgba = dynamic_cast<CCRGBAProtocol*>(node)) {
        GLubyte target = rgba->getOpacity();
        rgba->setOpacity(0);
        node->runAction(CCFadeTo::create(duration, target));
    }
    if (auto* kids = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(kids)) fadeInTree(child, duration);
    }
}

}

CollabEditorOverlay* CollabEditorOverlay::create(LevelEditorLayer* editor) {
    auto* ret = new CollabEditorOverlay();
    if (ret && ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabEditorOverlay::init(LevelEditorLayer* editor) {
    if (!CCNode::init()) return false;
    m_editor = editor;
    setID("collab-overlay"_spr);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    setContentSize(winSize);

    m_toastLayer = CCNode::create();
    m_toastLayer->setPosition({0.f, 0.f});
    m_toastLayer->setContentSize(winSize);
    m_toastLayer->setZOrder(500);
    addChild(m_toastLayer);

    m_voiceLayer = CCNode::create();
    m_voiceLayer->setPosition({0.f, 0.f});
    m_voiceLayer->setContentSize(winSize);
    m_voiceLayer->setZOrder(510);
    addChild(m_voiceLayer);

    m_statusBg = CCLayerColor::create({20, 24, 36, 0}, 220.f, kHudStatusH);
    m_statusBg->ignoreAnchorPointForPosition(false);
    m_statusBg->setAnchorPoint({0.f, 0.5f});
    m_statusBg->setZOrder(520);
    m_statusBg->setVisible(false);
    addChild(m_statusBg);

    m_statusBanner = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusBanner->setScale(0.45f);
    m_statusBanner->setPosition({110.f, kHudStatusH / 2.f});
    m_statusBg->addChild(m_statusBanner);

    m_controls = CCMenu::create();
    auto* chatSprite = ButtonSprite::create("Chat", "goldFont.fnt", "GJ_button_02.png", 0.45f);
    m_chatButton = CCMenuItemExt::createSpriteExtra(
        chatSprite,
        [](CCMenuItemSpriteExtra*) {
            auto* scene = CCDirector::sharedDirector()->getRunningScene();
            if (scene && scene->getChildByID("collab-chat"_spr)) return;
            if (auto* popup = CollabChatPopup::create()) popup->show();
        }
    );
    m_chatButton->setID("collab-chat-button"_spr);
    m_chatButton->setVisible(CollabManager::get().connected());
    m_controls->setContentSize({kHudStatusX - kHudLeft - 6.f, kChipHeight});
    m_controls->ignoreAnchorPointForPosition(false);
    m_controls->setAnchorPoint({0.f, 0.5f});
    m_controls->addChild(m_chatButton);
    m_controls->setLayout(
        RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Start)
    );
    m_controls->updateLayout();
    m_controls->setZOrder(525);
    addChild(m_controls);

    layoutHudBar();

    m_uiShowListener = paimon::editor::EditorUIShowEvent().listen(
        [this](EditorUI* ui, bool shown) {
            if (!ui || ui->m_editorLayer != m_editor) return false;
            m_editorUiHidden = !shown;
            applyVisibility();
            return false;
        }
    );

    CollabManager::get().setOverlay(this);
    schedule(schedule_selector(CollabEditorOverlay::refresh), 0.1f);
    schedule(schedule_selector(CollabEditorOverlay::updateVoice));
    return true;
}

CollabEditorOverlay::~CollabEditorOverlay() {
    // Unregister before teardown; messages can arrive during playtest or polling.
    m_uiShowListener.destroy();
    CollabManager::get().clearOverlay(this);

    for (auto& [id, tag] : m_tags) {
        if (tag && tag->getParent()) tag->removeFromParent();
    }
    m_tags.clear();
    for (auto& [id, sel] : m_selections) {
        if (sel.draw && sel.draw->getParent()) sel.draw->removeFromParent();
        if (sel.label && sel.label->getParent()) sel.label->removeFromParent();
    }
    m_selections.clear();
    for (auto& [id, cam] : m_cameras) {
        if (cam.ghostRoot && cam.ghostRoot->getParent()) cam.ghostRoot->removeFromParent();
        if (cam.trail && cam.trail->getParent()) cam.trail->removeFromParent();
        if (cam.label && cam.label->getParent()) cam.label->removeFromParent();
    }
    m_cameras.clear();
    for (auto& [id, z] : m_workZones) {
        if (z.draw && z.draw->getParent()) z.draw->removeFromParent();
        if (z.label && z.label->getParent()) z.label->removeFromParent();
    }
    m_workZones.clear();
    for (auto& flash : m_flashes) {
        if (flash.node && flash.node->getParent()) flash.node->removeFromParent();
    }
    m_flashes.clear();
    if (m_heatDraw && m_heatDraw->getParent()) m_heatDraw->removeFromParent();
    m_heatDraw = nullptr;
}

void CollabEditorOverlay::clearSelectionNode(int clientId) {
    auto it = m_selections.find(clientId);
    if (it == m_selections.end()) return;
    if (it->second.draw && it->second.draw->getParent()) it->second.draw->removeFromParent();
    if (it->second.label && it->second.label->getParent()) it->second.label->removeFromParent();
    m_selections.erase(it);
}

void CollabEditorOverlay::onPeerSelectionCleared(int clientId) {
    clearSelectionNode(clientId);
}

void CollabEditorOverlay::onPeerSelection(int clientId, std::string const& name,
                                          std::vector<CCRect> const& rects) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer) return;
    if (rects.empty()) {
        clearSelectionNode(clientId);
        return;
    }

    auto color = peerColor(clientId);
    float zoom = overlayZoom();
    auto border = drawColor(color, 0.9f);
    float lineWidth = outlineWidth(zoom, 1.6f);

    SelectionOverlay* slot = nullptr;
    auto it = m_selections.find(clientId);
    if (it != m_selections.end() && it->second.draw && it->second.draw->getParent()) {
        slot = &it->second;
        slot->draw->clear();
    } else {
        clearSelectionNode(clientId);
        SelectionOverlay overlay;
        overlay.draw = CCDrawNode::create();
        if (!overlay.draw) return;
        overlay.draw->setZOrder(8900);
        overlay.draw->setVisible(m_presenceVisible);
        objectLayer->addChild(overlay.draw);
        m_selections[clientId] = std::move(overlay);
        slot = &m_selections[clientId];
    }

    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (auto const& r : rects) {
        minX = std::min(minX, r.origin.x);
        minY = std::min(minY, r.origin.y);
        maxX = std::max(maxX, r.origin.x + r.size.width);
        maxY = std::max(maxY, r.origin.y + r.size.height);
        slot->draw->drawRect(r, kNoFill, lineWidth, border, BorderAlignment::Inside);
    }

    CCLabelBMFont* label = (slot->label && slot->label->getParent()) ? slot->label.data() : nullptr;
    if (!label) {
        label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        if (label) {
            label->setZOrder(8901);
            objectLayer->addChild(label);
            slot->label = label;
        }
    }
    if (label) {
        label->setString(name.c_str());
        label->setColor(color);
        label->setOpacity(220);
        label->setScale(zoom > 0.f ? std::clamp(0.4f / zoom, 0.08f, 5.f) : 0.4f);
        label->setPosition({(minX + maxX) * 0.5f, maxY + 10.f});
        label->setVisible(m_presenceVisible);
        label->stopAllActions();
        label->runAction(CCSequence::create(
            CCDelayTime::create(6.f),
            CCFadeOut::create(1.5f),
            nullptr
        ));
    }
}

void CollabEditorOverlay::clearCameraNode(int clientId) {
    auto it = m_cameras.find(clientId);
    if (it == m_cameras.end()) return;
    if (it->second.ghostRoot && it->second.ghostRoot->getParent()) it->second.ghostRoot->removeFromParent();
    if (it->second.trail && it->second.trail->getParent()) it->second.trail->removeFromParent();
    if (it->second.label && it->second.label->getParent()) it->second.label->removeFromParent();
    m_cameras.erase(it);
}

void CollabEditorOverlay::onPeerCameraCleared(int clientId) {
    clearCameraNode(clientId);
}

void CollabEditorOverlay::rebuildTrail(CameraOverlay& slot, int clientId) {
    if (!slot.trail) return;
    slot.trail->clear();
    auto color = peerColor(clientId);
    size_t n = slot.trailPts.size();
    if (n < 2) return;
    float zoom = overlayZoom();
    for (size_t i = 1; i < n; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(n - 1);
        auto c = drawColor(color, 0.05f + 0.25f * t);
        float r = outlineWidth(zoom, 0.7f + 1.3f * t);
        auto a0 = slot.trailPts[i - 1];
        auto a1 = slot.trailPts[i];
        int steps = 3;
        for (int s = 0; s <= steps; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(steps);
            CCPoint p{a0.x + (a1.x - a0.x) * u, a0.y + (a1.y - a0.y) * u};
            slot.trail->drawDot(p, r, c);
        }
    }
}

void CollabEditorOverlay::drainTrails(float dt) {
    for (auto& [id, cam] : m_cameras) {
        if (cam.trailPts.empty()) continue;
        cam.sinceMove += dt;
        if (cam.sinceMove < kTrailDrainEvery) continue;
        cam.sinceMove = 0.f;
        cam.trailPts.pop_front();
        rebuildTrail(cam, id);
    }
}

void CollabEditorOverlay::onPeerCamera(int clientId, std::string const& name, float x, float y,
                                       bool visible, PeerAppearance const& appearance) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer) return;

    auto color = peerColor(clientId);
    bool wantsCustomCursor = customCursorsEnabled() && appearance.hasCustomCursor;
    CameraOverlay* slot = nullptr;
    auto it = m_cameras.find(clientId);
    if (it != m_cameras.end() && it->second.customCursorRequested != wantsCustomCursor) {
        clearCameraNode(clientId);
        it = m_cameras.end();
    }
    if (it != m_cameras.end()) {
        slot = &it->second;
    } else {
        CameraOverlay overlay;
        overlay.customCursorRequested = wantsCustomCursor;
        overlay.trail = CCDrawNode::create();
        if (!overlay.trail) return;
        overlay.trail->setZOrder(8840);
        objectLayer->addChild(overlay.trail);

        auto* root = CCNode::create();
        root->setZOrder(8860);
        objectLayer->addChild(root);
        overlay.ghostRoot = root;

        if (wantsCustomCursor) {
            if (auto* cursor = createRemoteCursor(appearance)) {
                root->addChild(cursor, 1);
                overlay.customCursor = true;
            }
        }

        if (!overlay.customCursor) {
            int iconID = appearance.hasIcon ? std::max(1, appearance.iconID) : 1;
            if (auto* player = SimplePlayer::create(iconID)) {
                IconType type = appearance.hasIcon ? peerIconTypeLocal(appearance.iconType) : IconType::Cube;
                if (type != IconType::Cube) player->updatePlayerFrame(iconID, type);
                if (auto* gm = GameManager::get(); appearance.hasIcon && gm) {
                    player->setColor(gm->colorForIdx(std::clamp(appearance.color1, 0, 1000)));
                    player->setSecondColor(gm->colorForIdx(std::clamp(appearance.color2, 0, 1000)));
                    if (appearance.glowEnabled) {
                        player->setGlowOutline(gm->colorForIdx(std::clamp(appearance.glowColor, 0, 1000)));
                    } else {
                        player->disableGlowOutline();
                    }
                } else {
                    player->setColor(color);
                    player->setSecondColor({40, 42, 55});
                    player->disableGlowOutline();
                }
                player->setOpacity(200);
                player->setScale(0.55f);
                player->setPosition({0.f, 0.f});
                root->addChild(player, 1);
            } else {
                auto* dot = CCDrawNode::create();
                dot->drawDot({0.f, 0.f}, 8.f, drawColor(color, 0.9f));
                root->addChild(dot, 1);
            }

            auto* ring = CCDrawNode::create();
            ring->drawDot({0.f, 0.f}, 14.f, drawColor(color, 0.16f));
            root->addChild(ring, 0);
        }

        m_cameras[clientId] = std::move(overlay);
        slot = &m_cameras[clientId];
    }

    if (visible && (slot->trailPts.empty() ||
        std::hypot(x - slot->x, y - slot->y) >= kTrailMinDist)) {
        slot->trailPts.push_back({x, y});
        while (slot->trailPts.size() > kTrailMaxPts) slot->trailPts.pop_front();
        slot->sinceMove = 0.f;
        rebuildTrail(*slot, clientId);
    }
    slot->x = x;
    slot->y = y;

    if (slot->ghostRoot) {
        slot->ghostRoot->setPosition({x, y});
        float zoom = objectLayer->getScale();
        float baseScale = slot->customCursor ? 1.f : 0.55f;
        float sc = zoom > 0.f ? baseScale / zoom : baseScale;
        slot->ghostRoot->setScale(sc);
        slot->ghostRoot->setVisible(visible && m_presenceVisible);
    }
    if (slot->trail) slot->trail->setVisible(visible && m_presenceVisible);

    CCLabelBMFont* label = (slot->label && slot->label->getParent()) ? slot->label.data() : nullptr;
    if (!label) {
        label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        if (label) {
            label->setZOrder(8861);
            objectLayer->addChild(label);
            slot->label = label;
        }
    }
    if (label) {
        label->setString(name.c_str());
        label->setColor(color);
        label->setOpacity(235);
        float zoom = objectLayer->getScale();
        label->setScale(zoom > 0.f ? std::clamp(0.34f / zoom, 0.08f, 4.f) : 0.34f);
        label->setPosition({x, y + 22.f});
        label->setVisible(visible && m_presenceVisible);
    }
}

void CollabEditorOverlay::clearWorkZoneNode(int clientId) {
    auto it = m_workZones.find(clientId);
    if (it == m_workZones.end()) return;
    if (it->second.draw && it->second.draw->getParent()) it->second.draw->removeFromParent();
    if (it->second.label && it->second.label->getParent()) it->second.label->removeFromParent();
    m_workZones.erase(it);
}

void CollabEditorOverlay::onPeerWorkZoneCleared(int clientId) {
    clearWorkZoneNode(clientId);
}

void CollabEditorOverlay::onPeerWorkZone(int clientId, std::string const& name,
                                         float x, float y, float w, float h) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer || w <= 0.f || h <= 0.f) return;

    auto color = peerColor(clientId);
    WorkZoneOverlay* slot = nullptr;
    auto it = m_workZones.find(clientId);
    if (it != m_workZones.end() && it->second.draw && it->second.draw->getParent()) {
        slot = &it->second;
        slot->draw->clear();
    } else {
        clearWorkZoneNode(clientId);
        WorkZoneOverlay z;
        z.draw = CCDrawNode::create();
        if (!z.draw) return;
        z.draw->setZOrder(8830);
        z.draw->setVisible(m_presenceVisible);
        objectLayer->addChild(z.draw);
        m_workZones[clientId] = std::move(z);
        slot = &m_workZones[clientId];
    }

    ccColor4F border = drawColor(color, 0.55f);
    float zoom = overlayZoom();
    slot->draw->drawRect(CCRect{x, y, w, h}, kNoFill, outlineWidth(zoom, 1.2f), border,
                         BorderAlignment::Inside);

    CCLabelBMFont* label = (slot->label && slot->label->getParent()) ? slot->label.data() : nullptr;
    if (!label) {
        label = CCLabelBMFont::create("", "chatFont.fnt");
        if (label) {
            label->setZOrder(8831);
            label->setAnchorPoint({0.f, 1.f});
            objectLayer->addChild(label);
            slot->label = label;
        }
    }
    if (label) {
        auto text = fmt::format("{} - trabajando", name);
        label->setString(text.c_str());
        label->setColor(color);
        label->setOpacity(150);
        label->setScale(zoom > 0.f ? std::clamp(0.32f / zoom, 0.07f, 3.5f) : 0.32f);
        label->setPosition({x + 4.f, y + h - 4.f});
        label->setVisible(m_presenceVisible);
    }
}

void CollabEditorOverlay::onPeerPing(int clientId, std::string const& name, float x, float y) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer || !m_presenceVisible) return;
    auto color = peerColor(clientId);
    float zoom = overlayZoom();
    float base = zoom > 0.f ? 1.f / zoom : 1.f;

    if (auto* ring = CCDrawNode::create()) {
        ring->setZOrder(9005);
        ring->setPosition({x, y});
        ring->setScale(base);
        objectLayer->addChild(ring);
        ring->drawDot({0.f, 0.f}, 6.f, drawColor(color, 0.85f));
        ring->drawDot({0.f, 0.f}, 18.f, drawColor(color, 0.2f));
        ring->runAction(CCSequence::create(
            CCScaleTo::create(0.55f, base * 2.4f),
            CCCallFunc::create(ring, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
    if (auto* label = CCLabelBMFont::create(
            fmt::format("{} - mira aqui", name).c_str(), "chatFont.fnt")) {
        label->setZOrder(9006);
        label->setColor(color);
        label->setPosition({x, y + 28.f});
        label->setScale(zoom > 0.f ? std::clamp(0.4f / zoom, 0.1f, 4.f) : 0.4f);
        objectLayer->addChild(label);
        label->runAction(CCSequence::create(
            CCDelayTime::create(1.6f),
            CCFadeOut::create(0.7f),
            CCCallFunc::create(label, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
}

void CollabEditorOverlay::redrawHeatmap() {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer || !m_presenceVisible) return;

    if (!m_heatDraw || !m_heatDraw->getParent()) {
        m_heatDraw = CCDrawNode::create();
        if (!m_heatDraw) return;
        m_heatDraw->setZOrder(8820);
        objectLayer->addChild(m_heatDraw);
    }
    m_heatDraw->clear();
    auto samples = CollabManager::get().heatmapSamples(100);
    for (auto const& s : samples) {
        // Warm gradient: yellow -> orange by intensity, kept as a faint haze so
        // it never washes out the objects underneath.
        float t = s.intensity;
        ccColor3B warm{
            255,
            static_cast<GLubyte>(std::clamp(250.f - t * 120.f, 90.f, 255.f)),
            static_cast<GLubyte>(std::clamp(110.f * (1.f - t), 0.f, 110.f)),
        };
        m_heatDraw->drawDot({s.x, s.y}, 10.f + 20.f * t, drawColor(warm, 0.04f + 0.1f * t));
    }
}

float CollabEditorOverlay::overlayZoom() const {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    float zoom = objectLayer ? objectLayer->getScale() : 1.f;
    return zoom > 0.f ? zoom : 1.f;
}

float CollabEditorOverlay::hudRowY() const {
    float top = kDefaultToolbarHeight;
    auto* ui = m_editor ? m_editor->m_editorUI : nullptr;
    if (ui) {
        if (ui->m_toolbarHeight > 0.f) top = ui->m_toolbarHeight;
        if (auto* tabs = ui->m_tabsMenu; tabs && tabs->isVisible() && tabs->getChildren()) {
            for (auto* tab : CCArrayExt<CCNode*>(tabs->getChildren())) {
                if (!tab->isVisible()) continue;
                auto box = tab->boundingBox();
                top = std::max(top, tabs->convertToWorldSpace({box.getMidX(), box.getMaxY()}).y);
            }
        }
    }
    return top + kHudToolbarGap;
}

void CollabEditorOverlay::layoutHudBar() {
    m_hudWin = CCDirector::sharedDirector()->getWinSize();
    m_hudRowY = hudRowY();

    if (m_controls) m_controls->setPosition({kHudLeft, m_hudRowY});
    if (m_statusBg) m_statusBg->setPosition({kHudStatusX, m_hudRowY});
    layoutVoiceChips();
}

void CollabEditorOverlay::applyVisibility() {
    bool const playing = m_editor && m_editor->m_playbackMode == PlaybackMode::Playing;
    bool const show = !m_editorUiHidden && !playing;
    if (show == m_hudVisible) return;
    m_hudVisible = show;
    m_presenceVisible = show;

    if (m_toastLayer) m_toastLayer->setVisible(show);
    if (m_voiceLayer) m_voiceLayer->setVisible(show);
    if (m_controls) m_controls->setVisible(show);
    if (m_statusBg && !show) m_statusBg->setVisible(false);

    if (!show) {
        for (auto& [id, tag] : m_tags) {
            if (tag) tag->setVisible(false);
        }
    }
    for (auto& [id, sel] : m_selections) {
        if (sel.draw) sel.draw->setVisible(show);
        if (sel.label) sel.label->setVisible(show);
    }
    for (auto& [id, cam] : m_cameras) {
        if (cam.ghostRoot) cam.ghostRoot->setVisible(show);
        if (cam.trail) cam.trail->setVisible(show);
        if (cam.label) cam.label->setVisible(show);
    }
    for (auto& [id, zone] : m_workZones) {
        if (zone.draw) zone.draw->setVisible(show);
        if (zone.label) zone.label->setVisible(show);
    }
    if (m_heatDraw) m_heatDraw->setVisible(show);
}

void CollabEditorOverlay::updateStatusBanner() {
    if (!m_statusBg || !m_statusBanner) return;
    auto& mgr = CollabManager::get();
    auto st = static_cast<int>(mgr.state());
    bool recovering = mgr.isRecovering();
    std::string status = mgr.status();

    bool show = recovering || st == static_cast<int>(ConnState::Connecting) ||
                (st == static_cast<int>(ConnState::Connected) &&
                 (status.find("Sincroniz") != std::string::npos ||
                  status.find("reconect") != std::string::npos ||
                  status.find("Reconect") != std::string::npos ||
                  status.find("Desync") != std::string::npos ||
                  status.find("Subiendo") != std::string::npos ||
                  status.find("Siguiendo") != std::string::npos));

    if (!show) {
        if (m_statusBg->isVisible()) m_statusBg->setVisible(false);
        m_lastConnState = st;
        m_lastRecovering = recovering;
        m_lastStatus.clear();
        return;
    }

    if (st == m_lastConnState && recovering == m_lastRecovering && status == m_lastStatus &&
        m_statusBg->isVisible()) {
        return;
    }
    m_lastConnState = st;
    m_lastRecovering = recovering;
    m_lastStatus = status;

    std::string text = recovering ? "Reconectando a la sala..." : status;
    if (text.empty()) text = "Conectando...";
    if (mgr.followClientId() > 0) text += "  -  F para soltar";
    m_statusBanner->setString(text.c_str());
    m_statusBanner->limitLabelWidth(220.f, 0.45f, 0.2f);
    float w = std::max(140.f, m_statusBanner->getContentSize().width * m_statusBanner->getScale() + 24.f);
    m_statusBg->setContentSize({w, kHudStatusH});
    m_statusBanner->setPosition({w / 2.f, kHudStatusH / 2.f});
    m_statusBg->setOpacity(200);
    m_statusBg->setVisible(true);
}

void CollabEditorOverlay::refresh(float dt) {
    if (dt <= 0.f) dt = 0.1f;
    if (m_chatButton) m_chatButton->setVisible(CollabManager::get().connected());
    applyVisibility();
    sweepFlashes(dt);
    drainTrails(dt);
    if (!m_hudVisible) return;

    if (!m_hudWin.equals(CCDirector::sharedDirector()->getWinSize()) ||
        std::abs(hudRowY() - m_hudRowY) > 0.5f) {
        layoutHudBar();
    }
    updateStatusBanner();

    m_heatRedrawAge += dt;
    if (m_heatRedrawAge >= kHeatRedrawEvery) {
        m_heatRedrawAge = 0.f;
        redrawHeatmap();
    }

    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (objectLayer) {
        float zoom = objectLayer->getScale();
        if (zoom > 0.f && std::abs(zoom - m_lastOverlayZoom) > 0.0001f) {
            m_lastOverlayZoom = zoom;
            float scale = std::clamp(kTagBaseScale / zoom, 0.1f, 6.f);
            for (auto& [id, tag] : m_tags) {
                if (tag && tag->getParent() && tag->isVisible()) tag->setScale(scale);
            }
            float camScale = std::clamp(0.34f / zoom, 0.08f, 4.f);
            float ghostSc = std::clamp(0.55f / zoom, 0.12f, 1.4f);
            for (auto& [id, cam] : m_cameras) {
                if (cam.label && cam.label->getParent() && cam.label->isVisible()) {
                    cam.label->setScale(camScale);
                }
                if (cam.ghostRoot && cam.ghostRoot->getParent()) {
                    cam.ghostRoot->setScale(ghostSc);
                }
                rebuildTrail(cam, id);
            }
            auto& mgr = CollabManager::get();
            for (auto const& [id, sel] : mgr.peerSelections()) {
                onPeerSelection(id, sel.name, sel.rects);
            }
            for (auto const& [id, zone] : mgr.peerWorkZones()) {
                onPeerWorkZone(id, zone.name, zone.x, zone.y, zone.w, zone.h);
            }
        }
    }
}

void CollabEditorOverlay::onRemoteEdit(int clientId, std::string const& name, CCPoint worldPos, bool isDelete) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer || !m_presenceVisible) return;

    auto color = peerColor(clientId);

    if (m_flashes.size() < kMaxConcurrentFlashes) {
        if (auto* flash = CCSprite::create("square02b_001.png")) {
            flash->setPosition(worldPos);
            flash->setScale(0.4f);
            flash->setColor(isDelete ? ccColor3B{255, 70, 70} : color);
            flash->setOpacity(110);
            flash->setZOrder(9000);
            objectLayer->addChild(flash);
            flash->runAction(CCSequence::create(
                CCFadeOut::create(kFlashLife),
                CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            m_flashes.push_back({flash, 0.f});
        }
    }

    auto it = m_tags.find(clientId);
    CCLabelBMFont* tag = (it != m_tags.end()) ? it->second.data() : nullptr;
    if (!tag || !tag->getParent()) {
        tag = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        if (!tag) return;
        tag->setZOrder(9500);
        objectLayer->addChild(tag);
        m_tags[clientId] = tag;
    }
    tag->setString(name.c_str());
    tag->setColor(color);
    tag->setPosition(worldPos + CCPoint{0.f, 14.f});
    float zoom = objectLayer->getScale();
    tag->setScale(zoom > 0.f ? std::clamp(kTagBaseScale / zoom, 0.1f, 6.f) : kTagBaseScale);
    tag->setVisible(true);
    tag->stopAllActions();
    tag->setOpacity(255);
    tag->runAction(CCSequence::create(
        CCDelayTime::create(1.6f),
        CCFadeOut::create(0.8f),
        CCHide::create(),
        nullptr
    ));
}

void CollabEditorOverlay::sweepFlashes(float dt) {
    for (size_t i = 0; i < m_flashes.size();) {
        auto& flash = m_flashes[i];
        flash.age += dt;
        auto* node = flash.node.data();
        bool done = !node || !node->getParent() || flash.age > kFlashLife * 2.f;
        if (done) {
            if (node && node->getParent()) node->removeFromParent();
            m_flashes.erase(m_flashes.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

void CollabEditorOverlay::onChat(ChatMessage const& msg) {
    showToast(msg);
}

CCNode* CollabEditorOverlay::buildToast(ChatMessage const& msg) {
    auto* content = buildChatLine(msg, 0.5f);
    if (!content) return nullptr;

    float cw = content->getContentSize().width * content->getScaleX();
    float ch = content->getContentSize().height * content->getScaleY();
    cw = std::clamp(cw, 40.f, 340.f);

    float w = cw + 22.f;
    float h = std::max(ch + 12.f, 24.f);

    auto* toast = CCNode::create();
    toast->setAnchorPoint({0.5f, 0.5f});
    toast->setContentSize({w, h});

    auto* bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({w, h});
    bg->setPosition({w / 2.f, h / 2.f});
    bg->setColor({12, 14, 22});
    bg->setOpacity(195);
    toast->addChild(bg);

    content->setAnchorPoint({0.5f, 0.5f});
    content->setPosition({w / 2.f, h / 2.f});
    toast->addChild(content);

    return toast;
}

void CollabEditorOverlay::showToast(ChatMessage const& msg) {
    if (!m_toastLayer || !m_hudVisible) return;

    auto* toast = buildToast(msg);
    if (!toast) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    toast->setPosition({
        winSize.width + toast->getContentSize().width / 2.f + 12.f,
        winSize.height - kToastTopMargin - toast->getContentSize().height / 2.f,
    });
    m_toastLayer->addChild(toast);
    m_toasts.emplace_back(toast);
    fadeInTree(toast, kToastEnter * 0.75f);

    while (static_cast<int>(m_toasts.size()) > kMaxToasts) {
        dismissToast(m_toasts.front());
    }

    layoutToasts();

    toast->runAction(CCSequence::create(
        CCDelayTime::create(kToastEnter + kToastHold),
        CCCallFuncO::create(this, callfuncO_selector(CollabEditorOverlay::onToastExpired), toast),
        nullptr
    ));
}

void CollabEditorOverlay::layoutToasts() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    float y = winSize.height - kToastTopMargin;
    for (int p = static_cast<int>(m_toasts.size()) - 1; p >= 0; --p) {
        auto* toast = m_toasts[p].data();
        if (!toast) continue;
        float w = toast->getContentSize().width;
        float h = toast->getContentSize().height;
        CCPoint target{winSize.width - kToastRightMargin - w / 2.f, y - h / 2.f};
        y -= h + kToastGap;

        toast->stopActionByTag(kToastMoveTag);
        auto* move = CCEaseOut::create(CCMoveTo::create(kToastEnter, target), 3.f);
        move->setTag(kToastMoveTag);
        toast->runAction(move);
    }
}

void CollabEditorOverlay::onToastExpired(CCObject* sender) {
    dismissToast(typeinfo_cast<CCNode*>(sender));
}

void CollabEditorOverlay::dismissToast(CCNode* toast) {
    if (!toast) return;
    auto it = std::find_if(m_toasts.begin(), m_toasts.end(),
        [toast](geode::Ref<CCNode> const& r) { return r.data() == toast; });
    if (it == m_toasts.end()) return;
    m_toasts.erase(it);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    toast->stopAllActions();
    fadeOutTree(toast, kToastExit);
    float outX = winSize.width + toast->getContentSize().width / 2.f + 16.f;
    toast->runAction(CCSequence::create(
        CCEaseIn::create(CCMoveTo::create(kToastExit, {outX, toast->getPositionY()}), 2.f),
        CCCallFunc::create(toast, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));

    layoutToasts();
}


void CollabEditorOverlay::buildVoiceChip(int clientId, std::string const& name, VoiceChip& chip) {
    auto* nameLabel = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
    nameLabel->setScale(0.4f);
    nameLabel->limitLabelWidth(84.f, 0.4f, 0.15f);
    float nameW = nameLabel->getContentSize().width * nameLabel->getScale();

    constexpr float kTextX = 28.f;
    float barW = std::max(nameW, 36.f);
    float w = kTextX + barW + 8.f;
    float h = kChipHeight;

    auto* root = CCNode::create();
    root->setContentSize({w, h});
    root->setAnchorPoint({0.5f, 0.5f});

    auto* bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({w, h});
    bg->setColor({10, 12, 20});
    bg->setOpacity(180);
    bg->setPosition({w / 2.f, h / 2.f});
    root->addChild(bg);

    auto color = clientId == 0 ? ccColor3B{140, 255, 140} : peerColor(clientId);

    auto* dot = CCDrawNode::create();
    dot->drawDot({0.f, 0.f}, 8.f, ccColor4F{color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f});
    dot->setPosition({14.f, h / 2.f});
    root->addChild(dot, 1);

    char initial = '?';
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            initial = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            break;
        }
    }
    auto* letter = CCLabelBMFont::create(std::string(1, initial).c_str(), "bigFont.fnt");
    letter->setScale(0.3f);
    letter->setColor({20, 22, 30});
    letter->setPosition({14.f, h / 2.f + 0.5f});
    root->addChild(letter, 2);

    nameLabel->setAnchorPoint({0.f, 1.f});
    nameLabel->setPosition({kTextX, h - 4.f});
    root->addChild(nameLabel, 1);

    auto* track = CCLayerColor::create({0, 0, 0, 130}, barW, 3.5f);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint({0.f, 0.f});
    track->setPosition({kTextX, 5.f});
    root->addChild(track, 1);

    auto* fill = CCLayerColor::create({color.r, color.g, color.b, 255}, barW, 3.5f);
    fill->ignoreAnchorPointForPosition(false);
    fill->setAnchorPoint({0.f, 0.f});
    fill->setPosition({kTextX, 5.f});
    fill->setScaleX(0.f);
    root->addChild(fill, 2);

    chip.root = root;
    chip.barFill = fill;
    chip.width = w;
}

void CollabEditorOverlay::updateVoice(float dt) {
    if (!m_voiceLayer) return;
    auto& voice = CollabVoice::get();

    std::vector<SpeakingInfo> want;
    if (CollabManager::get().connected()) {
        want = voice.speakingNow();
        if (voice.transmitting()) want.push_back({0, "Tu", voice.localLevel()});
    }

    bool changed = false;
    std::unordered_set<int> active;
    for (auto const& s : want) {
        active.insert(s.clientId);
        auto it = m_voiceChips.find(s.clientId);
        if (it == m_voiceChips.end()) {
            VoiceChip chip;
            buildVoiceChip(s.clientId, s.name, chip);
            chip.root->setScale(0.6f);
            chip.root->setVisible(false);
            m_voiceLayer->addChild(chip.root);
            it = m_voiceChips.emplace(s.clientId, std::move(chip)).first;
            changed = true;
        }
        it->second.target = std::clamp(s.level, 0.f, 1.f);
        it->second.silent = 0.f;
    }

    for (auto it = m_voiceChips.begin(); it != m_voiceChips.end();) {
        auto& chip = it->second;
        if (!active.count(it->first)) {
            chip.target = 0.f;
            chip.silent += dt;
            if (chip.silent > 0.6f) {
                auto* node = chip.root.data();
                node->stopAllActions();
                node->runAction(CCSequence::create(
                    CCEaseIn::create(CCScaleTo::create(0.18f, 0.f), 2.f),
                    CCCallFunc::create(node, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
                it = m_voiceChips.erase(it);
                changed = true;
                continue;
            }
        }
        float k = chip.target > chip.shown ? 16.f : 6.f;
        chip.shown += (chip.target - chip.shown) * std::min(1.f, k * dt);
        if (chip.barFill) chip.barFill->setScaleX(std::clamp(chip.shown, 0.f, 1.f));
        ++it;
    }

    if (changed) layoutVoiceChips();
}

void CollabEditorOverlay::layoutVoiceChips() {
    if (m_voiceChips.empty()) return;

    std::vector<int> ids;
    ids.reserve(m_voiceChips.size());
    for (auto const& [id, chip] : m_voiceChips) ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    float x = kHudLeft;
    float y = (m_hudRowY > 0.f ? m_hudRowY : hudRowY()) + kHudRowGap;
    for (int id : ids) {
        auto& chip = m_voiceChips[id];
        float cx = x + chip.width / 2.f;
        x += chip.width + kChipGap;

        auto* node = chip.root.data();
        if (!chip.placed) {
            chip.placed = true;
            node->setPosition({cx, y});
            node->setVisible(true);
            node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)));
        } else {
            node->stopActionByTag(kChipMoveTag);
            auto* move = CCEaseOut::create(CCMoveTo::create(0.2f, {cx, y}), 2.5f);
            move->setTag(kChipMoveTag);
            node->runAction(move);
        }
    }
}

}
