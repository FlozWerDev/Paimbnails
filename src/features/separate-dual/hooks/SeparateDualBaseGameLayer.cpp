#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::IconSlot;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualBase, GJBaseGameLayer) {
    // Dress both live fighters from their own side of the vault.
    void refreshFighters() {
        auto vault = DualKitVault::get();
        vault->dressFighter(m_player1, Side::Primary);
        vault->dressFighter(m_player2, Side::Secondary);
    }

    // Refresh the dual-exit preview doll for the fighter that just left.
    void refreshExitDoll(PlayerObject* fighter) {
        auto vault = DualKitVault::get();
        auto doll = findFirstChildRecursive<SimplePlayer>(
            this, [](SimplePlayer* node) { return node->getZOrder() == 100; });
        if (!doll) return;

        IconSlot form = IconSlot::Cube;
        if (fighter->m_isShip) {
            form = fighter->m_isPlatformer ? IconSlot::Jetpack : IconSlot::Ship;
        } else if (fighter->m_isBall) {
            form = IconSlot::Ball;
        } else if (fighter->m_isBird) {
            form = IconSlot::Bird;
        } else if (fighter->m_isDart) {
            form = IconSlot::Dart;
        } else if (fighter->m_isRobot) {
            form = IconSlot::Robot;
        } else if (fighter->m_isSpider) {
            form = IconSlot::Spider;
        } else if (fighter->m_isSwing) {
            form = IconSlot::Swing;
        }

        IconType dollType = IconType::Cube;
        switch (form) {
            case IconSlot::Ship: dollType = IconType::Ship; break;
            case IconSlot::Ball: dollType = IconType::Ball; break;
            case IconSlot::Bird: dollType = IconType::Ufo; break;
            case IconSlot::Dart: dollType = IconType::Wave; break;
            case IconSlot::Robot: dollType = IconType::Robot; break;
            case IconSlot::Spider: dollType = IconType::Spider; break;
            case IconSlot::Swing: dollType = IconType::Swing; break;
            case IconSlot::Jetpack: dollType = IconType::Jetpack; break;
            default: break;
        }
        doll->updatePlayerFrame(vault->slotIcon(form, Side::Secondary), dollType);
    }

    void resetPlayer() {
        if (!moduleEnabled()) return GJBaseGameLayer::resetPlayer();
        if (!this->m_isPracticeMode) {
            DualKitVault::get()->resetRunState();
        }
        GJBaseGameLayer::resetPlayer();
        refreshFighters();
    }

    bool init() {
        if (!moduleEnabled()) return GJBaseGameLayer::init();
        DualKitVault::get()->resetRunState();
        DualKitVault::get()->ensureBurstArt(
            DualKitVault::get()->slotIcon(IconSlot::Death, Side::Secondary));
        return GJBaseGameLayer::init();
    }

    void onExit() {
        GJBaseGameLayer::onExit();
        if (!moduleEnabled()) return;
        DualKitVault::get()->resetRunState();
        DualKitVault::get()->releaseBurstArt(
            DualKitVault::get()->slotIcon(IconSlot::Death, Side::Secondary));
        DualKitVault::get()->m_exhaustMain = nullptr;
        DualKitVault::get()->m_exhaustSecond = nullptr;
    }

    void playExitDualEffect(PlayerObject* p0) {
        GJBaseGameLayer::playExitDualEffect(p0);
        if (!moduleEnabled()) return;
        if (!p0 || (p0 != m_player1 && p0 != m_player2)) return;

        auto vault = DualKitVault::get();
        if (p0 == m_player1 && Mod::get()->getSettingValue<bool>("separate-dual-exit-switch")
            && vault->exitSwapArmed()) {
            vault->flipLead();
            refreshFighters();
        }
        refreshExitDoll(p0);
    }

    void createPlayer() {
        if (!moduleEnabled()) return GJBaseGameLayer::createPlayer();
        DualKitVault::get()->setSpawning(true);
        GJBaseGameLayer::createPlayer();
        DualKitVault::get()->setSpawning(false);
    }
};
