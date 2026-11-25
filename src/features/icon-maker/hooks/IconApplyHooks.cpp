// no-moreicons fallback: after SimplePlayer and PlayerObject frame updates,
// injects the active custom icon's compiled frames and exact colors.

#include "../services/IconApplier.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
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

class $modify(PaimonIconMakerPlayerObject, PlayerObject) {
    $override
    void updatePlayerFrame(int frame) {
        PlayerObject::updatePlayerFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Cube);
    }

    $override
    void updatePlayerShipFrame(int frame) {
        PlayerObject::updatePlayerShipFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Ship);
    }

    $override
    void updatePlayerRollFrame(int frame) {
        PlayerObject::updatePlayerRollFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Ball);
    }

    $override
    void updatePlayerBirdFrame(int frame) {
        PlayerObject::updatePlayerBirdFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Ufo);
    }

    $override
    void updatePlayerDartFrame(int frame) {
        PlayerObject::updatePlayerDartFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Wave);
    }

    $override
    void updatePlayerSwingFrame(int frame) {
        PlayerObject::updatePlayerSwingFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Swing);
    }

    $override
    void updatePlayerJetpackFrame(int frame) {
        PlayerObject::updatePlayerJetpackFrame(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Jetpack);
    }

    $override
    void createRobot(int frame) {
        PlayerObject::createRobot(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Robot);
    }

    $override
    void createSpider(int frame) {
        PlayerObject::createSpider(frame);
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_maker::IconApplier::get().applyToPlayerObject(this, IconType::Spider);
    }
};
