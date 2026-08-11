// Fallback de aplicación sin MoreIcons: tras updatePlayerFrame, inyecta los
// frames compilados del icono activo en las capas del SimplePlayer.

#include "../services/IconApplier.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/SimplePlayer.hpp>

using namespace geode::prelude;

class $modify(PaimonIconMakerSimplePlayer, SimplePlayer) {
    $override
    void updatePlayerFrame(int iconId, IconType type) {
        SimplePlayer::updatePlayerFrame(iconId, type);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().onUpdatePlayerFrame(this, iconId, type);
    }
};
