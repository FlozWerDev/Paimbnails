#include "ScoreCellHoverWatcher.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include <Geode/utils/cocos.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::scorecell {

namespace {
    constexpr int kHoverTag    = 0x48565200;
    constexpr int kEntranceTag = 0x454E5400;

    ccBlendFunc additiveBlend() {
        return ccBlendFunc{GL_SRC_ALPHA, GL_ONE};
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
    this->setID("paimon-hover-watcher");
    this->scheduleUpdate();
    return true;
}

void ScoreCellHoverWatcher::setTransformTarget(CCNode* target,
                                               float baseScaleX, float baseScaleY,
                                               CCPoint basePos, float baseRot) {
    m_target = target;
    m_hasTarget = target != nullptr;
    m_baseScaleX = baseScaleX;
    m_baseScaleY = baseScaleY;
    m_basePos = basePos;
    m_baseRot = baseRot;
}

void ScoreCellHoverWatcher::update(float) {
    if (paimon::isRuntimeShuttingDown()) return;

    auto* cell = this->getParent();
    if (!cell) return;
    auto* cellParent = cell->getParent();
    if (!cellParent) return;

    CCRect rect = cell->boundingBox();
    rect.origin = cellParent->convertToWorldSpace(rect.origin);

    bool inside = rect.containsPoint(geode::cocos::getMousePos());
    if (inside == m_hovered) return;

    m_hovered = inside;
    if (inside) enterHover();
    else exitHover();
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
    if (m_glow && m_glow->getParent() == cell) return;

    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;

    auto glow = CCLayerColor::create(ccc4(255, 255, 255, 0));
    if (!glow) return;
    glow->setContentSize(cs);
    glow->setPosition({0.f, 0.f});
    glow->setZOrder(2);
    glow->setBlendFunc(additiveBlend());
    glow->setID("paimon-hover-glow");
    cell->addChild(glow);
    m_glow = glow;
}

void ScoreCellHoverWatcher::startShine() {
    auto* cell = this->getParent();
    if (!cell) return;
    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;

    stopShine();

    auto stencil = paimon::SpriteHelper::createRectStencil(cs.width, cs.height);
    auto clip = paimon::ScissorClipNode::create(stencil);
    if (!clip) return;
    clip->setContentSize(cs);
    clip->setPosition({0.f, 0.f});
    clip->setZOrder(3);
    clip->setID("paimon-hover-shine");

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
