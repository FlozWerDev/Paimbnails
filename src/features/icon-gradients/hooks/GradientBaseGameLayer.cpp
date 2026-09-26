// Re-shades the stashed dual doll when the dual effect plays, after zilko's
// "Icon Gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "GradientBaseGameLayer.hpp"
#include "GradientSimplePlayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientBaseGameLayer::playExitDualEffect(PlayerObject* p0) {
    bool second = p0 == m_player2;
    auto f = m_fields.self();

    if (!p0->isVanillaPlayer() || !moduleEnabled()) {
        GJBaseGameLayer::playExitDualEffect(p0);
        return;
    }

    f->isExitingDual = true;

    GJBaseGameLayer::playExitDualEffect(p0);

    if (auto icon = static_cast<GradientSimplePlayer*>(f->dualSimplePlayer))
        GradientUtils::paintMenuIcon(icon, second, 1000);

    f->isExitingDual = false;
    f->dualSimplePlayer = nullptr;
}
