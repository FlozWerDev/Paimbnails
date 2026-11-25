#include "TextSwapAnimation.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FluidReveal.hpp"
#include <Geode/binding/MultilineBitmapFont.hpp>
#include <Geode/binding/TextArea.hpp>
#include <algorithm>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate::ui {
namespace {

constexpr int kLiftActionTag = 0x54535750;
constexpr float kRise = 6.f;
constexpr float kDuration = .26f;
constexpr float kLineStagger = .045f;
// FluidReveal caps its start delay at this value; the lift matches it
constexpr float kMaxDelay = .15f;

// resting position of a lifting node. A swap that lands mid-animation must
// restart from the real position, not from the frame it interrupted.
class LiftRest : public CCObject {
public:
    CCPoint position;

    static LiftRest* create(CCPoint const& position) {
        auto* rest = new LiftRest();
        rest->position = position;
        rest->autorelease();
        return rest;
    }
};

void liftIn(CCNode* node, float delay) {
    std::string const key = "paimon-text-swap-rest"_spr;
    auto rest = node->getPosition();
    if (node->getActionByTag(kLiftActionTag)) {
        if (auto* saved = typeinfo_cast<LiftRest*>(node->getUserObject(key))) {
            rest = saved->position;
        }
    }
    node->stopActionByTag(kLiftActionTag);
    node->setUserObject(key, LiftRest::create(rest));
    node->setPosition(rest - CCPoint{0.f, kRise});

    auto* action = CCSequence::create(
        CCDelayTime::create(paimon::ui::motionDuration(delay)),
        CCEaseSineOut::create(CCMoveTo::create(paimon::ui::motionDuration(kDuration), rest)),
        nullptr);
    action->setTag(kLiftActionTag);
    node->runAction(action);
}

void animateNode(CCNode* node, float delay) {
    if (!node) return;
    paimon::fluid::revealNode(node, {.fadeDuration = kDuration, .startDelay = delay});
    liftIn(node, delay);
}

}

char const* textSwapMarker() {
    return "paimon-text-swap"_spr;
}

void animateTextSwap(CCNode* textNode) {
    if (!textNode || paimon::isRuntimeShuttingDown() || !paimon::ui::motionEnabled()) return;

    if (auto* area = typeinfo_cast<TextArea*>(textNode)) {
        auto* lines = area->m_label ? area->m_label->m_lines : nullptr;
        if (lines && lines->count() > 0) {
            float delay = 0.f;
            for (auto* line : CCArrayExt<CCNode*>(lines)) {
                animateNode(line, delay);
                delay = std::min(delay + kLineStagger, kMaxDelay);
            }
            return;
        }
    }
    animateNode(textNode, 0.f);
}

}
