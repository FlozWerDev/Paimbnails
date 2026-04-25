#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <vector>
#include <utility>
#include <set>

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCNode;
using cocos2d::CCObject;

struct PlayerVisState {
    bool visible = true;
    bool regTrail = true;
    bool waveTrail = true;
    bool ghostTrail = true;
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

    std::vector<std::pair<CCNode*, bool>> otherParticles;
};

inline void paimTogglePlayer(PlayerObject* p, PlayerVisState& state, bool hide) {
    using namespace geode::prelude;

    if (!p) return;

    auto toggle = [&](CCNode* node, bool& stateVar, bool hideNode) {
        if (!node) return;
        if (hideNode) {
            stateVar = node->isVisible();
            node->setVisible(false);
        } else {
            node->setVisible(stateVar);
        }
    };

    auto isKnownPlayerNode = [&](CCNode* node) {
        return node == p->m_regularTrail ||
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
               node == p->m_dashFireSprite;
    };

    auto collectExtraDescendants = [&](auto&& self, CCNode* root, std::set<CCNode*>& seen) -> void {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;

            if (!seen.insert(node).second) {
                continue; // ya visitado — no recursar de nuevo
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
        state.otherParticles.clear();
        p->setVisible(false);

        toggle(p->m_regularTrail, state.regTrail, true);
        toggle(p->m_waveTrail, state.waveTrail, true);
        toggle(p->m_ghostTrail, state.ghostTrail, true);
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

        std::set<CCNode*> seen;
        collectExtraDescendants(collectExtraDescendants, p, seen);
    } else {
        p->setVisible(state.visible);

        toggle(p->m_regularTrail, state.regTrail, false);
        toggle(p->m_waveTrail, state.waveTrail, false);
        toggle(p->m_ghostTrail, state.ghostTrail, false);
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

        for (auto& pair : state.otherParticles) {
            if (pair.first) {
                pair.first->setVisible(pair.second);
            }
        }
        state.otherParticles.clear();
    }
}
