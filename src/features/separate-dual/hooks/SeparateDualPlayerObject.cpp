#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualPlayer, PlayerObject) {
    bool isVanillaPlayerSDI() const {
        return m_gameLayer && (this == m_gameLayer->m_player1 || this == m_gameLayer->m_player2);
    }

    bool isPlayer2SDI() {
        return isPlayer2SDI(m_gameLayer);
    }

    bool isPlayer2SDI(GJBaseGameLayer* gameLayer) {
        return gameLayer && ((gameLayer->m_player1 && !gameLayer->m_player2 && this != gameLayer->m_player1) || this == gameLayer->m_player2);
    }

    template <typename T>
    T resolveSDI(T const& org, T const& val1, T const& val2) {
        if (!isVanillaPlayerSDI()) return org;
        return this == m_gameLayer->m_player1 ? val1 : val2;
    }

    void setupStreak() {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::setupStreak();
        auto GM = GameManager::get();
        auto SDI = Helper::get();

        int orgStreak = GM->getPlayerStreak();
        int orgShipFire = GM->getPlayerShipFire();
        GM->m_playerStreak = SDI->getTrail(this->isPlayer2SDI());
        GM->m_playerShipFire = SDI->getShipTrail(this->isPlayer2SDI());

        PlayerObject::setupStreak();

        GM->m_playerStreak = orgStreak;
        GM->m_playerShipFire = orgShipFire;

        if (SDI->m_isP2Main != this->isPlayer2SDI()) {
            SDI->m_p2ShipFire = this->m_shipStreak;
        } else {
            SDI->m_p1ShipFire = this->m_shipStreak;
        }
    }

    void playDeathEffect() {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::playDeathEffect();
        auto GM = GameManager::get();
        auto SDI = Helper::get();

        int orgDeathEffect = GM->getPlayerDeathEffect();
        bool orgDeathExplode = GM->getGameVariable("0153");
        GM->m_playerDeathEffect = SDI->getDeathEffect(this->isPlayer2SDI());
        GM->setGameVariable("0153", SDI->getDeathExplode(this->isPlayer2SDI()));

        PlayerObject::playDeathEffect();

        GM->m_playerDeathEffect = orgDeathEffect;
        GM->setGameVariable("0153", orgDeathExplode);
    }

    void update(float delta) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::update(delta);
        ShipStreak orgShipStreak = this->m_shipStreakType;
        this->m_shipStreakType = static_cast<ShipStreak>(Helper::get()->getShipTrail(this->isPlayer2SDI()));

        PlayerObject::update(delta);

        this->m_shipStreakType = orgShipStreak;
    }

    bool init(int player, int ship, GJBaseGameLayer* gameLayer, CCLayer* layer, bool playLayer) {
        if (!moduleEnabled()) return PlayerObject::init(player, ship, gameLayer, layer, playLayer);
        auto SDI = Helper::get();
        if (!SDI->m_insideCreatePlayer) return PlayerObject::init(player, ship, gameLayer, layer, playLayer);
        return PlayerObject::init(
            this->isPlayer2SDI(gameLayer) ? SDI->getCube(true) : player,
            this->isPlayer2SDI(gameLayer) ? SDI->getShip(true) : ship,
            gameLayer,
            layer,
            playLayer
        );
    }

    void setColor(ccColor3B const& color) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::setColor(color);
        PlayerObject::setColor(resolveSDI(color, GameManager::get()->colorForIdx(Helper::get()->getColor1(false)), GameManager::get()->colorForIdx(Helper::get()->getColor1(true))));
    }

    void setSecondColor(ccColor3B const& color) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::setSecondColor(color);
        PlayerObject::setSecondColor(resolveSDI(color, GameManager::get()->colorForIdx(Helper::get()->getColor2(false)), GameManager::get()->colorForIdx(Helper::get()->getColor2(true))));
    }

    void updatePlayerFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerFrame(frame);
        PlayerObject::updatePlayerFrame(resolveSDI(frame, Helper::get()->getCube(false), Helper::get()->getCube(true)));
    }

    void updatePlayerShipFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerShipFrame(frame);
        PlayerObject::updatePlayerShipFrame(resolveSDI(frame, Helper::get()->getShip(false), Helper::get()->getShip(true)));
    }

    void updatePlayerRollFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerRollFrame(frame);
        PlayerObject::updatePlayerRollFrame(resolveSDI(frame, Helper::get()->getBall(false), Helper::get()->getBall(true)));
    }

    void updatePlayerBirdFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerBirdFrame(frame);
        PlayerObject::updatePlayerBirdFrame(resolveSDI(frame, Helper::get()->getUFO(false), Helper::get()->getUFO(true)));
    }

    void updatePlayerDartFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerDartFrame(frame);
        PlayerObject::updatePlayerDartFrame(resolveSDI(frame, Helper::get()->getWave(false), Helper::get()->getWave(true)));
    }

    void createRobot(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::createRobot(frame);
        PlayerObject::createRobot(resolveSDI(frame, Helper::get()->getRobot(false), Helper::get()->getRobot(true)));
    }

    void toggleRobotMode(bool enable, bool noEffects) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::toggleRobotMode(enable, noEffects);
        int target = resolveSDI(this->m_robotSprite->m_iconRequestID, Helper::get()->getRobot(false), Helper::get()->getRobot(true));
        if (this->m_robotSprite->m_iconRequestID != target) {
            this->createRobot(target);

            if (this->m_ghostType == GhostType::Enabled) {
                this->toggleGhostEffect(GhostType::Disabled);
            }
            this->toggleGhostEffect(this->m_ghostType);

            this->m_hasGlow = Helper::get()->getGlow(this->isPlayer2SDI());
            this->enableCustomGlowColor(GameManager::get()->colorForIdx(Helper::get()->getGlowColor(this->isPlayer2SDI())));
            this->updatePlayerGlow();
            this->updateGlowColor();
        }
        PlayerObject::toggleRobotMode(enable, noEffects);
    }

    void createSpider(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::createSpider(frame);
        PlayerObject::createSpider(resolveSDI(frame, Helper::get()->getSpider(false), Helper::get()->getSpider(true)));
    }

    void toggleSpiderMode(bool enable, bool noEffects) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::toggleSpiderMode(enable, noEffects);
        int target = resolveSDI(this->m_spiderSprite->m_iconRequestID, Helper::get()->getSpider(false), Helper::get()->getSpider(true));
        if (this->m_spiderSprite->m_iconRequestID != target) {
            this->createSpider(target);

            if (this->m_ghostType == GhostType::Enabled) {
                this->toggleGhostEffect(GhostType::Disabled);
            }
            this->toggleGhostEffect(this->m_ghostType);

            this->m_hasGlow = Helper::get()->getGlow(this->isPlayer2SDI());
            this->enableCustomGlowColor(GameManager::get()->colorForIdx(Helper::get()->getGlowColor(this->isPlayer2SDI())));
            this->updatePlayerGlow();
            this->updateGlowColor();
        }
        PlayerObject::toggleSpiderMode(enable, noEffects);
    }

    void updatePlayerSwingFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerSwingFrame(frame);
        PlayerObject::updatePlayerSwingFrame(resolveSDI(frame, Helper::get()->getSwing(false), Helper::get()->getSwing(true)));
    }

    void updatePlayerJetpackFrame(int frame) {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updatePlayerJetpackFrame(frame);
        PlayerObject::updatePlayerJetpackFrame(resolveSDI(frame, Helper::get()->getJetpack(false), Helper::get()->getJetpack(true)));
    }

    void updateGlowColor() {
        if (!moduleEnabled() || !isVanillaPlayerSDI()) return PlayerObject::updateGlowColor();
        if (this->isPlayer2SDI()) {
            enableCustomGlowColor(GameManager::get()->colorForIdx(Helper::get()->getGlowColor(true)));
        }
        PlayerObject::updateGlowColor();
    }
};
