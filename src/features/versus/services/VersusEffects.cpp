#include "VersusEffects.hpp"
#include "VersusGlobed.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr char const* kOverlayId = "versus-effects"_spr;
constexpr char const* kFogTopId = "versus-fog-top"_spr;
constexpr char const* kFogBottomId = "versus-fog-bottom"_spr;
constexpr char const* kFrostId = "versus-frost"_spr;

constexpr float kZoomIn = 1.35f;
constexpr float kZoomOut = 0.70f;
constexpr float kBombFuse = 4.f;

} // namespace

VersusEffects& VersusEffects::get() {
    static VersusEffects instance;
    return instance;
}

void VersusEffects::attach(PlayLayer* layer) {
    detach();
    m_layer = layer;
    m_cameraFactor = 1.f;
    m_cameraMirrored = false;
    m_bombTimer = 0.f;
    m_reflect = false;
}

void VersusEffects::detach() {
    if (m_layer) endAll();
    if (m_overlay) {
        m_overlay->removeFromParent();
        m_overlay = nullptr;
    }
    m_active.clear();
    m_layer = nullptr;
    m_reflect = false;
    m_cameraFactor = 1.f;
    m_cameraMirrored = false;
}

CCNode* VersusEffects::overlayRoot() {
    if (!m_layer) return nullptr;
    if (m_overlay) return m_overlay;

    m_overlay = CCNode::create();
    m_overlay->setID(kOverlayId);
    m_overlay->setPosition({0.f, 0.f});
    m_layer->addChild(m_overlay, 900);
    return m_overlay;
}

void VersusEffects::addBand(float heightFraction, ccColor4B const& color, char const* id) {
    auto* root = overlayRoot();
    if (!root || root->getChildByID(id)) return;

    auto const winSize = CCDirector::get()->getWinSize();
    auto* band = CCLayerColor::create(color, winSize.width, winSize.height * heightFraction);
    if (!band) return;
    band->setID(id);
    band->setPosition({0.f, id == std::string(kFogTopId)
        ? winSize.height * (1.f - heightFraction) : 0.f});
    band->setOpacity(0);
    band->runAction(CCFadeTo::create(0.25f, color.a));
    root->addChild(band);
}

void VersusEffects::removeOverlay(char const* id) {
    if (!m_overlay) return;
    if (auto* node = m_overlay->getChildByID(id)) {
        node->runAction(CCSequence::create(
            CCFadeTo::create(0.3f, 0), CCRemoveSelf::create(), nullptr));
    }
}

void VersusEffects::flash(ccColor4B const& color, float duration) {
    auto* root = overlayRoot();
    if (!root) return;

    auto const winSize = CCDirector::get()->getWinSize();
    auto* layer = CCLayerColor::create(color, winSize.width, winSize.height);
    if (!layer) return;
    layer->runAction(CCSequence::create(
        CCFadeTo::create(duration, 0), CCRemoveSelf::create(), nullptr));
    root->addChild(layer, 10);
}

void VersusEffects::apply(CardId card, bool fromRival) {
    if (!m_layer) return;

    auto const& def = cardAt(card);
    if (def.duration > 0.f) {
        auto it = std::find_if(m_active.begin(), m_active.end(),
            [card](ActiveEffect const& effect) { return effect.card == card; });
        if (it != m_active.end()) {
            it->remaining = def.duration;
            return;
        }
        m_active.push_back({card, def.duration, def.duration, fromRival});
    }
    begin(card, fromRival);
}

void VersusEffects::begin(CardId card, bool fromRival) {
    switch (card) {
        case CardId::Fog:
            addBand(0.28f, {12, 14, 24, 235}, kFogTopId);
            addBand(0.28f, {12, 14, 24, 235}, kFogBottomId);
            break;

        case CardId::Quake:
            // GD's own shake, so it reads exactly like a level trigger and
            // costs nothing to undo.
            m_layer->shakeCamera(cardAt(card).duration, 8.f, 0.04f);
            break;

        case CardId::Weight:
            if (m_layer->m_percentageLabel) m_layer->m_percentageLabel->setVisible(false);
            if (m_layer->m_progressBar) m_layer->m_progressBar->setVisible(false);
            break;

        case CardId::Noise:
            if (auto* engine = FMODAudioEngine::sharedEngine(); engine && !m_audioMuted) {
                // Whatever they were playing at is what they get back; muting
                // first would restore everyone to full volume.
                m_musicVolume = engine->getBackgroundMusicVolume();
                m_effectsVolume = engine->getEffectsVolume();
                m_audioMuted = true;
                engine->setBackgroundMusicVolume(0.f);
                engine->setEffectsVolume(0.f);
            }
            break;

        case CardId::Mask:
            if (m_layer->m_player1) m_layer->m_player1->setVisible(false);
            break;

        case CardId::Freeze:
            addBand(1.f, {180, 220, 255, 190}, kFrostId);
            break;

        case CardId::Bomb:
            m_bombTimer = kBombFuse;
            break;

        case CardId::Ghost:
            // Cast on ourselves, but it is the rival's client that has to stop
            // drawing us, so the receiving side is the one that acts.
            if (fromRival) gl::setRivalHidden(true);
            break;

        case CardId::Shield:
            gl::grantShield();
            break;

        case CardId::Checkpoint:
            m_layer->markCheckpoint();
            break;

        case CardId::Skull:
            if (m_layer->m_checkpointArray && m_layer->m_checkpointArray->count() > 0) {
                m_layer->m_checkpointArray->removeLastObject();
            }
            break;

        case CardId::Dispel:
            dispelAll();
            break;

        case CardId::Reflect:
            m_reflect = true;
            break;

        default:
            break;
    }
}

void VersusEffects::end(CardId card) {
    switch (card) {
        case CardId::Fog:
            removeOverlay(kFogTopId);
            removeOverlay(kFogBottomId);
            break;

        case CardId::Weight:
            if (m_layer && m_layer->m_percentageLabel) m_layer->m_percentageLabel->setVisible(true);
            if (m_layer && m_layer->m_progressBar) m_layer->m_progressBar->setVisible(true);
            break;

        case CardId::Noise:
            if (auto* engine = FMODAudioEngine::sharedEngine(); engine && m_audioMuted) {
                m_audioMuted = false;
                engine->setBackgroundMusicVolume(m_musicVolume);
                engine->setEffectsVolume(m_effectsVolume);
            }
            break;

        case CardId::Mask:
            if (m_layer && m_layer->m_player1) m_layer->m_player1->setVisible(true);
            break;

        case CardId::Freeze:
            removeOverlay(kFrostId);
            break;

        case CardId::Ghost:
            gl::setRivalHidden(false);
            break;

        case CardId::ZoomIn:
        case CardId::ZoomOut:
        case CardId::Mirror:
            // applyCameraTransforms puts the layer back on the next frame, once
            // it can see that nothing is left holding it.
            break;

        default:
            break;
    }
}

void VersusEffects::endAll() {
    auto const snapshot = m_active;
    for (auto const& effect : snapshot) end(effect.card);
    m_active.clear();

    restoreCamera();
}

void VersusEffects::restoreCamera() {
    if (m_cameraFactor == 1.f && !m_cameraMirrored) return;

    if (m_layer && m_layer->m_objectLayer) {
        // Puts both axes back, so a mirrored layer loses its sign here too.
        m_layer->m_objectLayer->setScale(
            m_layer->m_objectLayer->getScaleY() / m_cameraFactor);
    }
    m_cameraFactor = 1.f;
    m_cameraMirrored = false;
}

void VersusEffects::update(float dt) {
    if (!m_layer) return;

    for (auto& effect : m_active) effect.remaining -= dt;

    for (auto const& effect : m_active) {
        if (effect.remaining <= 0.f) end(effect.card);
    }
    std::erase_if(m_active, [](ActiveEffect const& effect) { return effect.remaining <= 0.f; });

    if (m_bombTimer > 0.f) {
        m_bombTimer -= dt;
        if (m_bombTimer <= 0.f) {
            m_bombTimer = 0.f;
            flash({255, 255, 255, 235}, 0.4f);
        }
    }

    applyCameraTransforms();
}

void VersusEffects::applyCameraTransforms() {
    auto* objects = m_layer->m_objectLayer;
    if (!objects) return;

    bool const mirror = has(CardId::Mirror);
    float factor = 1.f;
    if (has(CardId::ZoomIn)) factor *= kZoomIn;
    if (has(CardId::ZoomOut)) factor *= kZoomOut;

    if (factor == 1.f && !mirror) {
        restoreCamera();
        return;
    }

    // The level writes its own zoom triggers into this same scale, so ours comes
    // back out first; Y is the axis to read since mirror owns the sign.
    float const level = objects->getScaleY() / m_cameraFactor;

    // Written after the level's own update, so ours is what ends up on screen.
    objects->setScale(level * factor);
    if (mirror) objects->setScaleX(-level * factor);

    m_cameraFactor = factor;
    m_cameraMirrored = mirror;
}

void VersusEffects::dispelAll() {
    std::vector<CardId> incoming;
    for (auto const& effect : m_active) {
        if (effect.fromRival) incoming.push_back(effect.card);
    }
    for (auto card : incoming) end(card);
    std::erase_if(m_active, [](ActiveEffect const& effect) { return effect.fromRival; });

    m_bombTimer = 0.f;
}

bool VersusEffects::has(CardId card) const {
    return std::any_of(m_active.begin(), m_active.end(),
        [card](ActiveEffect const& effect) { return effect.card == card; });
}

bool VersusEffects::cardsLocked() const {
    return has(CardId::Lock);
}

bool VersusEffects::seesRival() const {
    return has(CardId::Eye);
}

bool VersusEffects::barsHidden() const {
    return has(CardId::Magnet);
}

bool VersusEffects::consumeReflect() {
    if (!m_reflect) return false;
    m_reflect = false;
    return true;
}

} // namespace paimon::versus
