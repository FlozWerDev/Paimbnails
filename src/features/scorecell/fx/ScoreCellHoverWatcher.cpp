#include "ScoreCellHoverWatcher.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/SoftEdgeFade.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../cursor/services/CursorManager.hpp"
#include <Geode/binding/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::scorecell {

namespace {
    constexpr int kHoverTag    = 0x48565200;
    constexpr int kEntranceTag = 0x454E5400;

    ccBlendFunc additiveBlend() {
        return ccBlendFunc{GL_SRC_ALPHA, GL_ONE};
    }

    constexpr float kIconSlidePx = 24.f;

    class FadedIconSprite : public CCSprite {
    public:
        static FadedIconSprite* create(CCTexture2D* texture) {
            auto* sprite = new FadedIconSprite();
            if (sprite && sprite->initWithTexture(texture)) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        void draw() override {
            if (paimon::drawSoftEdgeFade(this, {0.32f, 0.f, true})) return;
            CCSprite::draw();
        }
    };

    // SimplePlayer's child layers do not inherit opacity from its root.
    void setSubtreeOpacity(CCNode* node, GLubyte alpha) {
        if (!node) return;
        if (auto* rgba = geode::cast::typeinfo_cast<CCNodeRGBA*>(node)) rgba->setOpacity(alpha);
        if (auto* children = node->getChildren()) {
            for (auto* child : geode::cocos::CCArrayExt<CCNode*>(children)) setSubtreeOpacity(child, alpha);
        }
    }
}

ScoreCellHoverWatcher* ScoreCellHoverWatcher::create(std::string const& type, float intensity) {
    if (!paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return nullptr;

    auto ret = new ScoreCellHoverWatcher();
    if (ret && ret->init(type, intensity)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ScoreCellHoverWatcher::init(std::string const& type, float intensity) {
    if (!CCNode::init()) return false;
    m_type = type;
    m_intensity = std::clamp(intensity, 0.f, 1.f);
    this->setID("paimon-hover-watcher"_spr);
    this->scheduleUpdate();
    return true;
}

void ScoreCellHoverWatcher::setTransformTarget(CCNode* target,
                                               float baseScaleX, float baseScaleY,
                                               CCPoint basePos, float baseRot) {
    m_target = target;
    m_baseScaleX = baseScaleX;
    m_baseScaleY = baseScaleY;
    m_basePos = basePos;
    m_baseRot = baseRot;
}

void ScoreCellHoverWatcher::update(float dt) {
    if (paimon::isRuntimeShuttingDown()) return;

    auto* cell = this->getParent();
    if (!cell) return;
    if (!cell->getParent()) return;

#if defined(GEODE_IS_MOBILE)
    // No mouse on touch screens: taps highlight via the cursor service, and the
    // highlight sticks on the last-tapped cell as selection feedback.
    CCPoint pointer = CursorManager::get().pointerPos();
#else
    CCPoint pointer = geode::cocos::getMousePos();
#endif
    auto local = cell->convertToNodeSpace(pointer);
    auto size = cell->getContentSize();
    bool inside = CCRect(0.f, 0.f, size.width, size.height).containsPoint(local);
    for (auto* node = cell; node && inside; node = node->getParent()) {
        if (!node->isVisible()) {
            inside = false;
            break;
        }
        if (node != cell && geode::cast::typeinfo_cast<CCLayer*>(node)) {
            auto bounds = node->getContentSize();
            if (bounds.width > 0.f && bounds.height > 0.f) {
                auto point = node->convertToNodeSpace(pointer);
                inside = CCRect(0.f, 0.f, bounds.width, bounds.height).containsPoint(point);
            }
        }
    }
    if (inside == m_hovered) {
        updateIcon(dt);
        return;
    }

    m_hovered = inside;
    if (inside) enterHover();
    else exitHover();
    updateIcon(dt);
}

void ScoreCellHoverWatcher::enterHover() {
    if (m_type == "glow") {
        ensureGlow();
        if (m_glow) {
            m_glow->stopAllActions();
            m_glow->runAction(CCEaseSineOut::create(
                CCFadeTo::create(0.18f, static_cast<GLubyte>(70.f * m_intensity + 10.f))));
        }
    } else if (m_type == "shine") {
        startShine();
    } else {
        applyTransformHover(true);
    }
}

void ScoreCellHoverWatcher::exitHover() {
    if (m_type == "glow") {
        if (m_glow) {
            m_glow->stopAllActions();
            m_glow->runAction(CCEaseSineOut::create(CCFadeTo::create(0.25f, 0)));
        }
    } else if (m_type == "shine") {
        stopShine();
    } else {
        applyTransformHover(false);
    }
}

void ScoreCellHoverWatcher::applyTransformHover(bool on) {
    auto* t = m_target.data();
    if (!t || !t->getParent()) return;

    t->stopActionByTag(kHoverTag);

    CCActionInterval* act = nullptr;
    const float inDur = 0.18f;
    const float outDur = 0.24f;

    if (m_type == "scale") {
        float k = 1.f + 0.12f * m_intensity;
        act = on ? CCScaleTo::create(inDur, m_baseScaleX * k, m_baseScaleY * k)
                 : CCScaleTo::create(outDur, m_baseScaleX, m_baseScaleY);
    } else if (m_type == "lift") {
        float dy = 7.f * m_intensity;
        CCPoint to = on ? ccp(m_basePos.x, m_basePos.y + dy) : m_basePos;
        act = CCMoveTo::create(on ? inDur : outDur, to);
    } else if (m_type == "tilt") {
        float ang = on ? (6.f * m_intensity) : m_baseRot;
        act = CCRotateTo::create(on ? inDur : outDur, ang);
    }

    if (act) {
        auto eased = CCEaseSineInOut::create(act);
        eased->setTag(kHoverTag);
        t->runAction(eased);
    }
}

void ScoreCellHoverWatcher::ensureGlow() {
    auto* cell = this->getParent();
    if (!cell) return;
    if (m_glow && m_glow->getParent()) return;

    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;

    // Rounded clip matching the cell gradient: a square overlay would flash
    // white corners over the rounded background on every hover.
    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f);
    if (!stencil) return;
    auto clip = CCClippingNode::create(stencil);
    if (!clip) return;
    clip->setContentSize(cs);
    clip->setPosition({0.f, 0.f});
    clip->setAlphaThreshold(0.05f);
    clip->setZOrder(2);
    clip->setID("paimon-hover-glow"_spr);

    auto glow = CCLayerColor::create(ccc4(255, 255, 255, 0));
    if (!glow) return;
    glow->setContentSize(cs);
    glow->setPosition({0.f, 0.f});
    glow->setBlendFunc(additiveBlend());
    clip->addChild(glow);
    cell->addChild(clip);
    m_glow = glow;
}

void ScoreCellHoverWatcher::startShine() {
    auto* cell = this->getParent();
    if (!cell) return;
    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;

    stopShine();

    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f);
    if (!stencil) return;
    // Plain CCClippingNode: ScissorClipNode would take its scissor fast-path
    // (axis-aligned rect) and ignore the rounded stencil.
    auto clip = CCClippingNode::create(stencil);
    if (!clip) return;
    clip->setContentSize(cs);
    clip->setPosition({0.f, 0.f});
    clip->setAlphaThreshold(0.05f);
    clip->setZOrder(3);
    clip->setID("paimon-hover-shine"_spr);

    float barW = std::max(18.f, cs.width * 0.10f);
    auto bar = CCLayerColor::create(
        ccc4(255, 255, 255, static_cast<GLubyte>(80.f * m_intensity + 25.f)),
        barW, cs.height * 1.6f);
    if (!bar) return;
    bar->ignoreAnchorPointForPosition(false);
    bar->setAnchorPoint({0.5f, 0.5f});
    bar->setSkewX(20.f);
    bar->setBlendFunc(additiveBlend());
    bar->setPosition({-barW, cs.height / 2.f});
    clip->addChild(bar);

    cell->addChild(clip);
    m_shine = clip;

    float dur = std::max(0.4f, 0.9f / (0.5f + m_intensity));
    auto seq = CCSequence::create(
        CCMoveTo::create(dur, ccp(cs.width + barW, cs.height / 2.f)),
        CCMoveTo::create(0.f, ccp(-barW, cs.height / 2.f)),
        CCDelayTime::create(0.5f),
        nullptr);
    bar->runAction(CCRepeatForever::create(seq));
}

void ScoreCellHoverWatcher::stopShine() {
    if (m_shine) {
        m_shine->stopAllActions();
        if (m_shine->getParent()) m_shine->removeFromParent();
        m_shine = nullptr;
    }
}

void ScoreCellHoverWatcher::ensureIconBackdrop() {
    auto* cell = this->getParent();
    if (!cell) return;
    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;
    if (m_iconClip && m_iconClip->getParent()) {
        auto cur = m_iconClip->getContentSize();
        if (std::fabs(cur.width - cs.width) < 1.f && std::fabs(cur.height - cs.height) < 1.f) return;
        m_iconClip->removeFromParent();
        m_iconClip = nullptr;
        m_icon = nullptr;
        m_appliedMix = -1.f;
    }

    auto* scoreCell = geode::cast::typeinfo_cast<GJScoreCell*>(cell);
    if (!scoreCell || !scoreCell->m_score) return;
    auto* score = scoreCell->m_score;
    auto* gm = GameManager::sharedState();
    if (!gm) return;

    int iconID = score->m_iconID > 0 ? score->m_iconID : std::max(score->m_playerCube, 1);
    auto* player = SimplePlayer::create(iconID);
    if (!player) return;
    player->updatePlayerFrame(iconID, score->m_iconType);
    player->setColor(gm->colorForIdx(score->m_color1));
    player->setSecondColor(gm->colorForIdx(score->m_color2));
    if (score->m_glowEnabled) player->setGlowOutline(gm->colorForIdx(score->m_color3 > 0 ? score->m_color3 : score->m_color2));
    else player->disableGlowOutline();

    // The root owns no texture (its layers carry the pixels), so its size
    // reads 0x0: measure the main layer, else use the ~30px icon reference.
    float dim = std::max(player->getContentSize().width, player->getContentSize().height);
    if (dim <= 0.f && player->m_firstLayer) {
        dim = std::max(player->m_firstLayer->getContentSize().width,
                       player->m_firstLayer->getContentSize().height);
    }
    if (dim <= 0.f) dim = 30.f;
    player->setScale(cs.height * 1.45f / dim);
    CCPoint home = {cs.width * 0.82f, cs.height * 0.5f};

    CCNode* icon = player;
    int renderSize = static_cast<int>(std::ceil(cs.height * 1.85f));
    if (auto* render = CCRenderTexture::create(renderSize, renderSize)) {
        player->setPosition({renderSize * 0.5f, renderSize * 0.5f});
        render->beginWithClear(0.f, 0.f, 0.f, 0.f);
        player->visit();
        render->end();
        if (auto* sprite = FadedIconSprite::create(render->getSprite()->getTexture())) {
            sprite->setFlipY(true);
            sprite->setOpacity(0);
            icon = sprite;
        }
    }
    icon->setPosition(home);
    if (icon == player) setSubtreeOpacity(player, 0);

    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f);
    if (!stencil) return;
    auto clip = CCClippingNode::create(stencil);
    if (!clip) return;
    clip->setContentSize(cs);
    clip->setPosition({0.f, 0.f});
    clip->setAlphaThreshold(0.05f);
    clip->setZOrder(0);
    clip->setID("paimon-hover-icon"_spr);
    clip->addChild(icon);
    cell->addChild(clip);
    m_iconClip = clip;
    m_icon = icon;
    m_iconHome = home;
    clip->setVisible(m_iconMix > 0.01f);
}

void ScoreCellHoverWatcher::updateIcon(float dt) {
    if (!m_hovered && m_iconMix <= 0.f) return;
    if (m_hovered) ensureIconBackdrop();
    auto* icon = m_icon.data();
    if (!m_iconClip || !icon || !m_iconClip->getParent()) return;
    if (dt < 0.f) dt = 0.f;

    float target = m_hovered ? 1.f : 0.f;
    float speed = m_hovered ? 10.f : 8.f;
    m_iconMix += (target - m_iconMix) * (1.f - std::exp(-speed * dt));
    if (std::fabs(m_iconMix - target) < 0.002f) m_iconMix = target;
    if (m_iconMix == m_appliedMix) return;
    m_appliedMix = m_iconMix;

    float e = m_iconMix * m_iconMix * (3.f - 2.f * m_iconMix);
    icon->setPosition({m_iconHome.x + (1.f - e) * kIconSlidePx, m_iconHome.y});
    auto opacity = static_cast<GLubyte>(e * 215.f);
    if (auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(icon)) sprite->setOpacity(opacity);
    else setSubtreeOpacity(icon, opacity);
    m_iconClip->setVisible(m_iconMix > 0.01f);
}

void applyEntrance(CCNode* node, std::string const& type,
                   CCPoint finalPos, float finalScaleX, float finalScaleY) {
    if (!node || type == "none") return;
    if (!paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return;
    node->stopActionByTag(kEntranceTag);

    CCActionInterval* act = nullptr;

    if (type == "fade") {
        node->setScaleX(finalScaleX * 0.92f);
        node->setScaleY(finalScaleY * 0.92f);
        act = CCEaseSineOut::create(CCScaleTo::create(0.30f, finalScaleX, finalScaleY));
    } else if (type == "pop") {
        node->setScaleX(0.01f);
        node->setScaleY(0.01f);
        act = CCEaseBackOut::create(CCScaleTo::create(0.40f, finalScaleX, finalScaleY));
    } else if (type == "bounce") {
        node->setScaleX(0.01f);
        node->setScaleY(0.01f);
        act = CCEaseBounceOut::create(CCScaleTo::create(0.55f, finalScaleX, finalScaleY));
    } else if (type == "slide") {
        node->setPosition({finalPos.x + 40.f, finalPos.y});
        act = CCEaseSineOut::create(CCMoveTo::create(0.35f, finalPos));
    }

    if (act) {
        act->setTag(kEntranceTag);
        node->runAction(act);
    }
}

}
