// Menu-doll shading outside gameplay, rethought for Paimbnails.
//
// Idea credit: "Icon Gradients" by zilko
// (https://github.com/zilko144/icon-gradients-geode, unlicensed —
// all rights reserved). Independent implementation: same behavior (shade
// SimplePlayer dolls shown in icon previews, skipping in-game dolls),
// own expression.

#include "GradientSimplePlayer.hpp"
#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "GradientBaseGameLayer.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

void GradientSimplePlayer::updatePlayerFrame(int p0, IconType type) {
    SimplePlayer::updatePlayerFrame(p0, type);

    m_fields->m_type = type;

    // Stash the doll for the dual-exit effect while in-game.
    if (GJBaseGameLayer* bgl = GJBaseGameLayer::get()) {
        auto f = static_cast<GradientBaseGameLayer*>(bgl)->m_fields.self();
        if (f->isExitingDual) f->dualSimplePlayer = this;
        return;
    }

    if (!moduleEnabled() || !paimon::modules::isEnabled("paimbnails.paimonicons.global")) return;

    bool p2 = sdiEnabled()
        && CCDirector::get()->getRunningScene()->getChildByType<GJGarageLayer>(0)
        && sdiSaved<bool>("2pselected", false);

    Loader::get()->queueInMainThread([self = Ref(this), p2] {
        if (!self->getParent() || !typeinfo_cast<GJItemIcon*>(self->getParent())) return;
        if (CCSprite* spr = self->getChildByType<CCSprite>(0)) {
            if (spr->getOpacity() <= 120) return;
        }
        GradientUtils::paintMenuIcon(self, p2, 66);
    });
}
