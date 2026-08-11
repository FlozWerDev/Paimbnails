#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/misc_nodes/CCMotionStreak.h>
#include <vector>
#include <utility>
#include <set>

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCNode;
using cocos2d::CCObject;

struct PlayerVisState {
    bool visible = true;
    bool hidden = false;
    bool regTrail = true;
    bool regTrailDraw = true;
    bool shipStreak = true;
    bool shipStreakDraw = true;
    bool waveTrail = true;
    bool waveTrailDraw = true;
    bool ghostTrail = true;
    bool ghostTrailDraw = true;
    bool vehicleGroundPart = true;
    bool robotFire = true;
    
    bool playerGroundPart = true;
    bool trailingPart = true;
    bool shipClickPart = true;
    bool ufoClickPart = true;
    bool robotBurstPart = true;
    bool dashPart = true;
    bool swingBurstPart1 = true;
    bool swingBurstPart2 = true;
    bool landPart0 = true;
    bool landPart1 = true;
    bool dashFireSprite = true;
    bool swingFireMiddle = true;
    bool swingFireBottom = true;
    bool swingFireTop = true;
    bool dashSpritesContainer = true;

    // WeakRef: this state can outlive a frame (the preview popup keeps it while
    // open), and unknown mod-added player descendants may be destroyed in the
    // meantime. lock() skips dead nodes instead of touching freed memory.
    std::vector<std::pair<geode::WeakRef<CCNode>, bool>> otherParticles;
};

inline void paimTogglePlayer(PlayerObject* p, PlayerVisState& state, bool hide) {
    using namespace geode::prelude;

    if (!p) return;

    auto toggleHardStreak = [&](CCNode* node, bool& visibleStateVar, bool& drawStateVar, bool hideNode) {
        auto* streak = typeinfo_cast<HardStreak*>(node);
        if (!streak) {
            return false;
        }

        if (hideNode) {
            visibleStateVar = streak->isVisible();
            drawStateVar = streak->m_drawStreak;
            streak->stopStroke();
            streak->setVisible(false);
        } else {
            streak->setVisible(visibleStateVar);
            if (drawStateVar) {
                streak->resumeStroke();
            } else {
                streak->stopStroke();
            }
        }

        return true;
    };

    auto toggleMotionStreak = [&](CCNode* node, bool& visibleStateVar, bool& drawStateVar, bool hideNode) {
        auto* streak = typeinfo_cast<cocos2d::CCMotionStreak*>(node);
        if (!streak) {
            return false;
        }

        if (hideNode) {
            visibleStateVar = streak->isVisible();
            drawStateVar = streak->m_bStroke;
            streak->stopStroke();
            streak->setVisible(false);
        } else {
            streak->setVisible(visibleStateVar);
            if (drawStateVar) {
                streak->resumeStroke();
            } else {
                streak->stopStroke();
            }
        }

        return true;
    };

    auto toggle = [&](CCNode* node, bool& stateVar, bool hideNode) {
        if (!node) return;
        if (hideNode) {
            stateVar = node->isVisible();
            node->setVisible(false);
        } else {
            node->setVisible(stateVar);
        }
    };

    auto toggleTrail = [&](CCNode* node, bool& visibleStateVar, bool& drawStateVar, bool hideNode) {
        if (!node) return;
        if (toggleHardStreak(node, visibleStateVar, drawStateVar, hideNode)) return;
        if (toggleMotionStreak(node, visibleStateVar, drawStateVar, hideNode)) return;
        if (hideNode) {
            visibleStateVar = node->isVisible();
            node->setVisible(false);
        } else {
            node->setVisible(visibleStateVar);
        }
    };

    auto isKnownPlayerNode = [&](CCNode* node) {
        return node == p->m_regularTrail ||
               node == p->m_shipStreak ||
               node == p->m_waveTrail ||
               node == p->m_ghostTrail ||
               node == p->m_vehicleGroundParticles ||
               node == p->m_robotFire ||
               node == p->m_playerGroundParticles ||
               node == p->m_trailingParticles ||
               node == p->m_shipClickParticles ||
               node == p->m_ufoClickParticles ||
               node == p->m_robotBurstParticles ||
               node == p->m_dashParticles ||
               node == p->m_swingBurstParticles1 ||
               node == p->m_swingBurstParticles2 ||
               node == p->m_landParticles0 ||
               node == p->m_landParticles1 ||
               node == p->m_dashFireSprite ||
               node == p->m_swingFireMiddle ||
               node == p->m_swingFireBottom ||
               node == p->m_swingFireTop ||
               node == p->m_dashSpritesContainer;
    };

    auto collectExtraDescendants = [&](auto&& self, CCNode* root, std::set<CCNode*>& seen) -> void {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;

            if (!seen.insert(node).second) {
                continue; // already visited — don't recurse again
            }

            if (!isKnownPlayerNode(node) && node != p) {
                state.otherParticles.push_back({node, node->isVisible()});
                node->setVisible(false);
            }

            self(self, node, seen);
        }
    };

    if (hide) {
        state.visible = p->isVisible();
        state.hidden = p->m_isHidden;
        state.otherParticles.clear();
        p->toggleVisibility(false);
        p->setVisible(false);

        toggleTrail(p->m_regularTrail, state.regTrail, state.regTrailDraw, true);
        toggleTrail(p->m_shipStreak, state.shipStreak, state.shipStreakDraw, true);
        toggleTrail(p->m_waveTrail, state.waveTrail, state.waveTrailDraw, true);
        toggleTrail(p->m_ghostTrail, state.ghostTrail, state.ghostTrailDraw, true);
        toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, true);
        toggle(p->m_robotFire, state.robotFire, true);

        toggle(p->m_playerGroundParticles, state.playerGroundPart, true);
        toggle(p->m_trailingParticles, state.trailingPart, true);
        toggle(p->m_shipClickParticles, state.shipClickPart, true);
        toggle(p->m_ufoClickParticles, state.ufoClickPart, true);
        toggle(p->m_robotBurstParticles, state.robotBurstPart, true);
        toggle(p->m_dashParticles, state.dashPart, true);
        toggle(p->m_swingBurstParticles1, state.swingBurstPart1, true);
        toggle(p->m_swingBurstParticles2, state.swingBurstPart2, true);
        toggle(p->m_landParticles0, state.landPart0, true);
        toggle(p->m_landParticles1, state.landPart1, true);
        toggle(p->m_dashFireSprite, state.dashFireSprite, true);
        toggle(p->m_swingFireMiddle, state.swingFireMiddle, true);
        toggle(p->m_swingFireBottom, state.swingFireBottom, true);
        toggle(p->m_swingFireTop, state.swingFireTop, true);
        toggle(p->m_dashSpritesContainer, state.dashSpritesContainer, true);

        std::set<CCNode*> seen;
        collectExtraDescendants(collectExtraDescendants, p, seen);
    } else {
        p->toggleVisibility(!state.hidden);
        p->setVisible(state.visible);

        toggleTrail(p->m_regularTrail, state.regTrail, state.regTrailDraw, false);
        toggleTrail(p->m_shipStreak, state.shipStreak, state.shipStreakDraw, false);
        toggleTrail(p->m_waveTrail, state.waveTrail, state.waveTrailDraw, false);
        toggleTrail(p->m_ghostTrail, state.ghostTrail, state.ghostTrailDraw, false);
        toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, false);
        toggle(p->m_robotFire, state.robotFire, false);

        toggle(p->m_playerGroundParticles, state.playerGroundPart, false);
        toggle(p->m_trailingParticles, state.trailingPart, false);
        toggle(p->m_shipClickParticles, state.shipClickPart, false);
        toggle(p->m_ufoClickParticles, state.ufoClickPart, false);
        toggle(p->m_robotBurstParticles, state.robotBurstPart, false);
        toggle(p->m_dashParticles, state.dashPart, false);
        toggle(p->m_swingBurstParticles1, state.swingBurstPart1, false);
        toggle(p->m_swingBurstParticles2, state.swingBurstPart2, false);
        toggle(p->m_landParticles0, state.landPart0, false);
        toggle(p->m_landParticles1, state.landPart1, false);
        toggle(p->m_dashFireSprite, state.dashFireSprite, false);
        toggle(p->m_swingFireMiddle, state.swingFireMiddle, false);
        toggle(p->m_swingFireBottom, state.swingFireBottom, false);
        toggle(p->m_swingFireTop, state.swingFireTop, false);
        toggle(p->m_dashSpritesContainer, state.dashSpritesContainer, false);

        for (auto& pair : state.otherParticles) {
            if (auto node = pair.first.lock()) {
                node->setVisible(pair.second);
            }
        }
        state.otherParticles.clear();
    }
}
