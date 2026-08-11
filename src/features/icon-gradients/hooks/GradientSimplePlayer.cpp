#include "GradientSimplePlayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "GradientBaseGameLayer.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientSimplePlayer::updatePlayerFrame(int p0, IconType type) {
    SimplePlayer::updatePlayerFrame(p0, type);

    m_fields->m_type = type;

    auto bgl = GJBaseGameLayer::get();

    if (bgl) {
        auto f = static_cast<GradientBaseGameLayer*>(bgl)->m_fields.self();

        if (f->isExitingDual) {
            f->dualSimplePlayer = this;
        }
    }

    if (bgl || !moduleEnabled() || !paimon::modules::isEnabled("paimbnails.paimonicons.global")) {
        return;
    }

    bool p2 = false;
    if (sdiEnabled() && CCDirector::get()->getRunningScene()->getChildByType<GJGarageLayer>(0)) {
        p2 = sdiSaved<bool>("2pselected", false);
    }

    Loader::get()->queueInMainThread([self = Ref(this), type, p2] {
        if (!self->getParent()) return;
        if (!typeinfo_cast<GJItemIcon*>(self->getParent())) return;

        if (CCSprite* spr = self->getChildByType<CCSprite>(0)) {
            if (spr->getOpacity() <= 120) return;
        }

        Gradient gradient = GradientUtils::getGradient(type, p2);
        GradientUtils::applyGradient(self, gradient, false, p2, 66);
    });
}
