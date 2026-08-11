#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualPlay, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (!moduleEnabled()) return PlayLayer::destroyPlayer(player, object);
        if (player && !(player == m_player1 || player == m_player2)) return PlayLayer::destroyPlayer(player, object);
        Helper::get()->m_shouldSwap = false;
        PlayLayer::destroyPlayer(player, object);
        Helper::get()->m_shouldSwap = true;
    }
};
