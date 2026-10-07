#pragma once

#include <Geode/Geode.hpp>
#include "../core/RuntimeLifecycle.hpp"
#include "../ui/PaimonUI.hpp"
#include <algorithm>
#include <vector>

namespace paimon::fluid {

struct RevealOpts {
    float fadeDuration = 0.18f;
    float startDelay = 0.f;
    float stagger = 0.035f;
    bool recurse = true;
};

namespace detail {

inline constexpr int kFadeActionTag = 0x46414445;

inline bool forwardsOpacityWithoutCascade(cocos2d::CCNode* node) {
    return geode::cast::typeinfo_cast<TextArea*>(node) ||
        geode::cast::typeinfo_cast<MultilineBitmapFont*>(node) ||
        geode::cast::typeinfo_cast<cocos2d::CCLabelBMFont*>(node) ||
        geode::cast::typeinfo_cast<cocos2d::CCMenuItemSprite*>(node) ||
        geode::cast::typeinfo_cast<cocos2d::CCMenuItemLabel*>(node) ||
        geode::cast::typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(node);
}

inline void reveal(cocos2d::CCNode* node, RevealOpts const& opts, float delay, int depth) {
    if (!node || depth > 10) return;
    if (auto* rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol*>(node)) {
        auto const key = geode::Mod::get()->getID() + "/reveal-opacity";
        auto opacity = rgba->getOpacity();
        if (node->getActionByTag(kFadeActionTag)) {
            if (auto* saved = geode::cast::typeinfo_cast<cocos2d::CCInteger*>(node->getUserObject(key))) {
                opacity = static_cast<GLubyte>(saved->getValue());
            }
        }
        node->stopActionByTag(kFadeActionTag);
        if (!paimon::ui::motionEnabled()) {
            rgba->setOpacity(opacity);
        } else {
            // Preserve translucent panels when an interrupted reveal restarts.
            node->setUserObject(key, cocos2d::CCInteger::create(opacity));
            rgba->setOpacity(0);
            auto* action = cocos2d::CCSequence::create(
                cocos2d::CCDelayTime::create(paimon::ui::motionDuration(std::clamp(delay, 0.f, 0.15f))),
                cocos2d::CCEaseSineOut::create(cocos2d::CCFadeTo::create(
                    paimon::ui::motionDuration(std::max(0.01f, opts.fadeDuration)), opacity)), nullptr);
            action->setTag(kFadeActionTag);
            node->runAction(action);
        }
        if (rgba->isCascadeOpacityEnabled() || forwardsOpacityWithoutCascade(node)) return;
    }
    if (!opts.recurse) return;
    for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) {
        reveal(child, opts, delay, depth + 1);
    }
}

}

inline void revealNode(cocos2d::CCNode* node, RevealOpts opts = {}) {
    if (paimon::isRuntimeShuttingDown()) return;
    detail::reveal(node, opts, opts.startDelay, 0);
}

inline void revealSequential(std::vector<cocos2d::CCNode*> nodes, RevealOpts opts = {}) {
    if (paimon::isRuntimeShuttingDown()) return;
    float delay = opts.startDelay;
    for (auto* node : nodes) {
        if (!node) continue;
        detail::reveal(node, opts, delay, 0);
        delay += std::max(0.f, opts.stagger);
    }
}

inline void revealChildren(cocos2d::CCNode* container, RevealOpts opts = {}) {
    if (!container || paimon::isRuntimeShuttingDown()) return;
    std::vector<cocos2d::CCNode*> nodes;
    for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(container->getChildren())) {
        nodes.push_back(child);
    }
    revealSequential(std::move(nodes), opts);
}

}
