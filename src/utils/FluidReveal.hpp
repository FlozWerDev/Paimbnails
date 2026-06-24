#pragma once

// FluidReveal — sequential fade-in for UI. Nodes created in the same frame
// would pop in all at once; this hides them immediately (opacity 0) and reveals
// them with a staggered fade-in so the appearance feels fluid.
//
// Usage:
//   paimon::fluid::revealSequential({ title, menu, content }); // staggered
//   paimon::fluid::revealNode(panel);                          // one node
//   paimon::fluid::revealChildren(scrollContent);              // direct children

#include <Geode/Geode.hpp>
#include "MainThreadDelay.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <vector>

namespace paimon::fluid {

struct RevealOpts {
    float fadeDuration = 0.18f;  // fade-in duration per node
    float startDelay   = 0.0f;   // global delay before the first node
    float stagger      = 0.05f;  // time gap between nodes (the "sequential" part)
    bool  recurse      = true;   // if the node has no opacity, animate its RGBA children
};

namespace detail {

// tag to avoid stacking multiple fades on the same node ('FADE')
inline constexpr int kFadeActionTag = 0x46414445;

// Hide immediately (opacity 0). If the node isn't CCRGBAProtocol, recurse to the
// first RGBA descendant.
inline void prehide(cocos2d::CCNode* node, bool recurse, int depth) {
    if (!node || depth > 10) return;

    if (auto* rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol*>(node)) {
        rgba->setCascadeOpacityEnabled(true);
        node->stopActionByTag(kFadeActionTag);
        rgba->setOpacity(0);
        return; // cascade handles the children
    }

    if (!recurse) return;
    if (auto* children = node->getChildren()) {
        int count = static_cast<int>(children->count());
        for (int i = 0; i < count; ++i) {
            prehide(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)), recurse, depth + 1);
        }
    }
}

// Start the fade-in (opacity -> 255). Mirrors prehide()'s recursion.
inline void fadeIn(cocos2d::CCNode* node, float duration, bool recurse, int depth) {
    if (!node || depth > 10) return;

    if (auto* rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol*>(node)) {
        rgba->setCascadeOpacityEnabled(true);
        node->stopActionByTag(kFadeActionTag);
        auto* act = cocos2d::CCFadeTo::create(std::max(0.01f, duration), 255);
        act->setTag(kFadeActionTag);
        node->runAction(act);
        return;
    }

    if (!recurse) return;
    if (auto* children = node->getChildren()) {
        int count = static_cast<int>(children->count());
        for (int i = 0; i < count; ++i) {
            fadeIn(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)), duration, recurse, depth + 1);
        }
    }
}

} // namespace detail

// Reveal a single node with fade-in: hides it this frame and starts the fade
// after startDelay.
inline void revealNode(cocos2d::CCNode* node, RevealOpts opts = {}) {
    if (!node || paimon::isRuntimeShuttingDown()) return;

    detail::prehide(node, opts.recurse, 0);

    geode::Ref<cocos2d::CCNode> ref = node;
    float dur = opts.fadeDuration;
    bool  rec = opts.recurse;
    paimon::scheduleMainThreadDelay(std::max(0.f, opts.startDelay), [ref, dur, rec]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto* n = ref.data()) detail::fadeIn(n, dur, rec, 0);
    });
}

// Reveal a list of nodes in sequence (staggered by opts.stagger). All hide
// immediately, then each fades in turn.
inline void revealSequential(std::vector<cocos2d::CCNode*> nodes, RevealOpts opts = {}) {
    if (paimon::isRuntimeShuttingDown()) return;

    float t = std::max(0.f, opts.startDelay);
    for (auto* node : nodes) {
        if (!node) continue;

        detail::prehide(node, opts.recurse, 0);

        geode::Ref<cocos2d::CCNode> ref = node;
        float dur = opts.fadeDuration;
        bool  rec = opts.recurse;
        paimon::scheduleMainThreadDelay(t, [ref, dur, rec]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (auto* n = ref.data()) detail::fadeIn(n, dur, rec, 0);
        });
        t += std::max(0.f, opts.stagger);
    }
}

// Convenience: reveal a container's direct children, in order.
inline void revealChildren(cocos2d::CCNode* container, RevealOpts opts = {}) {
    if (!container || paimon::isRuntimeShuttingDown()) return;
    auto* children = container->getChildren();
    if (!children) return;

    std::vector<cocos2d::CCNode*> nodes;
    int count = static_cast<int>(children->count());
    nodes.reserve(count);
    for (int i = 0; i < count; ++i) {
        nodes.push_back(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)));
    }
    revealSequential(std::move(nodes), opts);
}

} // namespace paimon::fluid
