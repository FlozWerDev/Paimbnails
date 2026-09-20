#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualPlayer, PlayerObject) {
    bool isLiveFighter() const {
        if (!m_gameLayer) return false;
        return this == m_gameLayer->m_player1 || this == m_gameLayer->m_player2;
    }

    // True for the fighter driven by the second kit: player 2 in a dual
    // pair, or — while spawning, before player 2 is linked — anything that
    // is not the first fighter.
    bool drivesSecondKit() {
        return drivesSecondKit(m_gameLayer);
    }

    bool drivesSecondKit(GJBaseGameLayer* layer) {
        if (!layer) return false;
        if (this == layer->m_player2) return true;
        return layer->m_player1 && !layer->m_player2 && this != layer->m_player1;
    }

    Side kitSide() {
        return drivesSecondKit() ? Side::Secondary : Side::Primary;
    }

    template <typename T>
    T pickForFighter(T fallback, T primary, T secondary) {
        if (!isLiveFighter()) return fallback;
        return this == m_gameLayer->m_player1 ? primary : secondary;
    }

    void setupStreak() {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::setupStreak();
        auto gm = GameManager::get();
        auto vault = DualKitVault::get();
        Side side = kitSide();

        int liveStreak = gm->getPlayerStreak();
        int liveFire = gm->getPlayerShipFire();
        gm->m_playerStreak = vault->slotIcon(
            paimon::separate_dual::IconSlot::Trail, side);
        gm->m_playerShipFire = vault->slotIcon(
            paimon::separate_dual::IconSlot::ShipFire, side);

        PlayerObject::setupStreak();

        gm->m_playerStreak = liveStreak;
        gm->m_playerShipFire = liveFire;

        // Keep the streak node alive for dressFighter's ship-fire fitting.
        if (vault->leadIsSecondary() != (side == Side::Secondary)) {
            vault->m_exhaustSecond = this->m_shipStreak;
        } else {
            vault->m_exhaustMain = this->m_shipStreak;
        }
    }

    void playDeathEffect() {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::playDeathEffect();
        auto gm = GameManager::get();
        auto vault = DualKitVault::get();
        Side side = kitSide();

        int liveDeath = gm->getPlayerDeathEffect();
        bool liveBurst = gm->getGameVariable("0153");
        gm->m_playerDeathEffect = vault->slotIcon(
            paimon::separate_dual::IconSlot::Death, side);
        gm->setGameVariable("0153", vault->burstEnabled(side));

        PlayerObject::playDeathEffect();

        gm->m_playerDeathEffect = liveDeath;
        gm->setGameVariable("0153", liveBurst);
    }

    void update(float delta) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::update(delta);
        ShipStreak liveType = this->m_shipStreakType;
        this->m_shipStreakType = static_cast<ShipStreak>(DualKitVault::get()->slotIcon(
            paimon::separate_dual::IconSlot::ShipFire, kitSide()));

        PlayerObject::update(delta);

        this->m_shipStreakType = liveType;
    }

    bool init(int player, int ship, GJBaseGameLayer* gameLayer, CCLayer* layer, bool playLayer) {
        if (!moduleEnabled()) return PlayerObject::init(player, ship, gameLayer, layer, playLayer);
        auto vault = DualKitVault::get();
        if (!vault->isSpawning()) return PlayerObject::init(player, ship, gameLayer, layer, playLayer);
        // Spawned player 2 starts from the stored kit, player 1 from the args.
        bool second = drivesSecondKit(gameLayer);
        return PlayerObject::init(
            second ? vault->slotIcon(paimon::separate_dual::IconSlot::Cube, Side::Secondary) : player,
            second ? vault->slotIcon(paimon::separate_dual::IconSlot::Ship, Side::Secondary) : ship,
            gameLayer, layer, playLayer);
    }

    void applyGlowDress() {
        auto vault = DualKitVault::get();
        Side side = kitSide();
        this->m_hasGlow = vault->haloEnabled(side);
        this->enableCustomGlowColor(GameManager::get()->colorForIdx(vault->haloOf(side)));
        this->updatePlayerGlow();
        this->updateGlowColor();
    }

    void setColor(ccColor3B const& color) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::setColor(color);
        auto vault = DualKitVault::get();
        PlayerObject::setColor(pickForFighter(
            color,
            GameManager::get()->colorForIdx(vault->inkOf(Side::Primary)),
            GameManager::get()->colorForIdx(vault->inkOf(Side::Secondary))));
    }

    void setSecondColor(ccColor3B const& color) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::setSecondColor(color);
        auto vault = DualKitVault::get();
        PlayerObject::setSecondColor(pickForFighter(
            color,
            GameManager::get()->colorForIdx(vault->trimOf(Side::Primary)),
            GameManager::get()->colorForIdx(vault->trimOf(Side::Secondary))));
    }

    void updatePlayerFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Cube, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Cube, Side::Secondary)));
    }

    void updatePlayerShipFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerShipFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerShipFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Ship, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Ship, Side::Secondary)));
    }

    void updatePlayerRollFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerRollFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerRollFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Ball, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Ball, Side::Secondary)));
    }

    void updatePlayerBirdFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerBirdFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerBirdFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Bird, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Bird, Side::Secondary)));
    }

    void updatePlayerDartFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerDartFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerDartFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Dart, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Dart, Side::Secondary)));
    }

    void createRobot(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::createRobot(frame);
        auto vault = DualKitVault::get();
        PlayerObject::createRobot(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Robot, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Robot, Side::Secondary)));
    }

    void rebuildVehicleGlow() {
        if (this->m_ghostType == GhostType::Enabled) {
            this->toggleGhostEffect(GhostType::Disabled);
        }
        this->toggleGhostEffect(this->m_ghostType);
        applyGlowDress();
    }

    void toggleRobotMode(bool enable, bool noEffects) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::toggleRobotMode(enable, noEffects);
        auto vault = DualKitVault::get();
        int want = pickForFighter(
            this->m_robotSprite->m_iconRequestID,
            vault->slotIcon(paimon::separate_dual::IconSlot::Robot, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Robot, Side::Secondary));
        if (this->m_robotSprite->m_iconRequestID != want) {
            this->createRobot(want);
            rebuildVehicleGlow();
        }
        PlayerObject::toggleRobotMode(enable, noEffects);
    }

    void createSpider(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::createSpider(frame);
        auto vault = DualKitVault::get();
        PlayerObject::createSpider(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Spider, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Spider, Side::Secondary)));
    }

    void toggleSpiderMode(bool enable, bool noEffects) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::toggleSpiderMode(enable, noEffects);
        auto vault = DualKitVault::get();
        int want = pickForFighter(
            this->m_spiderSprite->m_iconRequestID,
            vault->slotIcon(paimon::separate_dual::IconSlot::Spider, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Spider, Side::Secondary));
        if (this->m_spiderSprite->m_iconRequestID != want) {
            this->createSpider(want);
            rebuildVehicleGlow();
        }
        PlayerObject::toggleSpiderMode(enable, noEffects);
    }

    void updatePlayerSwingFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerSwingFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerSwingFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Swing, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Swing, Side::Secondary)));
    }

    void updatePlayerJetpackFrame(int frame) {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updatePlayerJetpackFrame(frame);
        auto vault = DualKitVault::get();
        PlayerObject::updatePlayerJetpackFrame(pickForFighter(
            frame,
            vault->slotIcon(paimon::separate_dual::IconSlot::Jetpack, Side::Primary),
            vault->slotIcon(paimon::separate_dual::IconSlot::Jetpack, Side::Secondary)));
    }

    void updateGlowColor() {
        if (!moduleEnabled() || !isLiveFighter()) return PlayerObject::updateGlowColor();
        if (drivesSecondKit()) {
            enableCustomGlowColor(GameManager::get()->colorForIdx(
                DualKitVault::get()->haloOf(Side::Secondary)));
        }
        PlayerObject::updateGlowColor();
    }
};
