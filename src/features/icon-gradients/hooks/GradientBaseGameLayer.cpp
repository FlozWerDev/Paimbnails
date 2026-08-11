#include "GradientBaseGameLayer.hpp"
#include "GradientSimplePlayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientBaseGameLayer::playExitDualEffect(PlayerObject* p0) {
    if (!p0->isVanillaPlayer() || !moduleEnabled()) {
        GJBaseGameLayer::playExitDualEffect(p0);
        return;
    }

    auto f = m_fields.self();

    f->isExitingDual = true;

    GJBaseGameLayer::playExitDualEffect(p0);

    if (auto icon = static_cast<GradientSimplePlayer*>(f->dualSimplePlayer)) {
        GradientUtils::applyGradient(
            icon,
            GradientUtils::getGradient(icon->m_fields->m_type, p0 == m_player2),
            false,
            p0 == m_player2,
            1000
        );
    }

    f->isExitingDual = false;
    f->dualSimplePlayer = nullptr;
}
