#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "GradientPlayerObject.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

class $modify(GradientPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!moduleEnabled()) return;

        if (m_player1) {
            auto* p1 = static_cast<GradientPlayerObject*>(m_player1);
            p1->updateVisibility();
            p1->updateFlip(0.f);
        }

        if (m_player2) {
            auto* p2 = static_cast<GradientPlayerObject*>(m_player2);
            p2->updateVisibility();
            p2->updateFlip(0.f);
        }
    }
};
