#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>

using namespace geode::prelude;

class $modify(PaimonGameLevelManager, GameLevelManager) {
    $override void getOnlineLevels(GJSearchObject* object) {
        if (!object) {
            return GameLevelManager::getOnlineLevels(object);
        }

        GameLevelManager::getOnlineLevels(object);
    }
};
