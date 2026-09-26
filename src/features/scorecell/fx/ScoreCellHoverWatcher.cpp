#include "ScoreCellHoverWatcher.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../cursor/services/CursorManager.hpp"
#include <Geode/utils/cocos.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::scorecell {

namespace {
    constexpr int kHoverTag    = 0x48565200;

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

void ScoreCellHoverWatcher::update(float) {
    if (paimon::isRuntimeShuttingDown()) return;

    auto* cell = this->getParent();
    if (!cell) return;
    if (!cell->getParent()) return;

#if defined(GEODE_IS_MOBILE)
    // No mouse on touch: taps highlight via cursor service, sticking on last-tapped as selection.
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
    if (m_glow && m_glow->getParent()) return;

    auto cs = cell->getContentSize();
    if (cs.width <= 1.f || cs.height <= 1.f) return;

    // Rounded clip matching the gradient: square would flash white corners on hover.
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
    // Plain CCClippingNode: ScissorClipNode fast-paths axis-aligned rects and ignores rounded stencils.
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

}
