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
            static_cast<GradientPlayerObject*>(m_player1)->updateVisibility();
        }
        static_cast<GradientPlayerObject*>(m_player1)->updateFlip(0.f);

        if (m_player2) {
            static_cast<GradientPlayerObject*>(m_player2)->updateVisibility();
        }
        static_cast<GradientPlayerObject*>(m_player2)->updateFlip(0.f);
    }
};
