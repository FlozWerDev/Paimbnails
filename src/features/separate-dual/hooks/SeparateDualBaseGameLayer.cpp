#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualBase, GJBaseGameLayer) {
    void resetPlayer() {
        if (!moduleEnabled()) return GJBaseGameLayer::resetPlayer();
        if (!this->m_isPracticeMode) {
            Helper::get()->reset();
        }
        GJBaseGameLayer::resetPlayer();
        Helper::get()->setPlayerInfo(this->m_player1, false);
        Helper::get()->setPlayerInfo(this->m_player2, true);
    }

    bool init() {
        if (!moduleEnabled()) return GJBaseGameLayer::init();
        Helper::get()->reset();
        Helper::get()->loadDeathTextures(Helper::get()->getDeathEffect(true));
        return GJBaseGameLayer::init();
    }

    void onExit() {
        GJBaseGameLayer::onExit();
        if (!moduleEnabled()) return;
        Helper::get()->reset();
        Helper::get()->unloadDeathTextures(Helper::get()->getDeathEffect(true));
        Helper::get()->m_p1ShipFire = nullptr;
        Helper::get()->m_p2ShipFire = nullptr;
    }

    void playExitDualEffect(PlayerObject* p0) {
        GJBaseGameLayer::playExitDualEffect(p0);
        if (!moduleEnabled()) return;
        if (!p0 || (p0 != m_player1 && p0 != m_player2)) return;

        auto GM = GameManager::get();
        auto SDI = Helper::get();

        if (p0 == m_player1) {
            if (Mod::get()->getSettingValue<bool>("separate-dual-exit-switch") && SDI->m_shouldSwap) {
                SDI->swapAll();
                SDI->setPlayerInfo(m_player1, false);
                SDI->setPlayerInfo(m_player2, true);
            }

            if (auto player = findFirstChildRecursive<SimplePlayer>(this, [](SimplePlayer* node) { return node->getZOrder() == 100; })) {
                if (m_player1->m_isShip) {
                    if (m_player1->m_isPlatformer)
                        player->updatePlayerFrame(SDI->getJetpack(true), IconType::Jetpack);
                    else
                        player->updatePlayerFrame(SDI->getShip(true), IconType::Ship);
                } else if (m_player1->m_isBall) {
                    player->updatePlayerFrame(SDI->getBall(true), IconType::Ball);
                } else if (m_player1->m_isBird) {
                    player->updatePlayerFrame(SDI->getUFO(true), IconType::Ufo);
                } else if (m_player1->m_isDart) {
                    player->updatePlayerFrame(SDI->getWave(true), IconType::Wave);
                } else if (m_player1->m_isRobot) {
                    player->updatePlayerFrame(SDI->getRobot(true), IconType::Robot);
                } else if (m_player1->m_isSpider) {
                    player->updatePlayerFrame(SDI->getSpider(true), IconType::Spider);
                } else if (m_player1->m_isSwing) {
                    player->updatePlayerFrame(SDI->getSwing(true), IconType::Swing);
                } else {
                    player->updatePlayerFrame(SDI->getCube(true), IconType::Cube);
                }
            }
        } else if (p0 == m_player2) {
            if (auto player = findFirstChildRecursive<SimplePlayer>(this, [](SimplePlayer* node) { return node->getZOrder() == 100; })) {
                if (m_player2->m_isShip) {
                    if (m_player2->m_isPlatformer)
                        player->updatePlayerFrame(SDI->getJetpack(true), IconType::Jetpack);
                    else
                        player->updatePlayerFrame(SDI->getShip(true), IconType::Ship);
                } else if (m_player2->m_isBall) {
                    player->updatePlayerFrame(SDI->getBall(true), IconType::Ball);
                } else if (m_player2->m_isBird) {
                    player->updatePlayerFrame(SDI->getUFO(true), IconType::Ufo);
                } else if (m_player2->m_isDart) {
                    player->updatePlayerFrame(SDI->getWave(true), IconType::Wave);
                } else if (m_player2->m_isRobot) {
                    player->updatePlayerFrame(SDI->getRobot(true), IconType::Robot);
                } else if (m_player2->m_isSpider) {
                    player->updatePlayerFrame(SDI->getSpider(true), IconType::Spider);
                } else if (m_player2->m_isSwing) {
                    player->updatePlayerFrame(SDI->getSwing(true), IconType::Swing);
                } else {
                    player->updatePlayerFrame(SDI->getCube(true), IconType::Cube);
                }
            }
        }
    }

    void createPlayer() {
        if (!moduleEnabled()) return GJBaseGameLayer::createPlayer();
        Helper::get()->m_insideCreatePlayer = true;
        GJBaseGameLayer::createPlayer();
        Helper::get()->m_insideCreatePlayer = false;
    }
};
