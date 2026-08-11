#pragma once

// Separate Dual Icons: customize the 2nd player's kit (icons, colors, trails,
// death effect) independently from your own. Port of Weebify's
// separate-dual-icons-geode, gated by the paimbnails.separatedual.global module.

#include <Geode/Geode.hpp>
#include "../../core/modules/ModuleRegistry.hpp"

namespace paimon::separate_dual {

constexpr char const* kModuleId = "paimbnails.separatedual.global";

inline bool moduleEnabled() {
    return modules::isEnabled(kModuleId);
}

class Helper {
public:
    static Helper* get();

    void reset();
    void swapAll();
    bool isP2Selected();
    void setP2Selected(bool selected);
    void swapSavedKitWithGame();

    void loadDeathTextures(int id);
    void unloadDeathTextures(int id);
    void setPlayerInfo(PlayerObject* player, bool isP2);
    void setSimplePlayerInfo(SimplePlayer* player, IconType type, bool isP2);
    void setupNormalStreak(PlayerObject* player, bool isP2);
    void setupShipFire(PlayerObject* player, bool isP2);
    char const* getFrameForStreak(int shipFire, float delta);

    int getIconID(IconType type, bool isP2);
    int getCube(bool isP2);
    int getShip(bool isP2);
    int getBall(bool isP2);
    int getUFO(bool isP2);
    int getWave(bool isP2);
    int getRobot(bool isP2);
    int getSpider(bool isP2);
    int getSwing(bool isP2);
    int getJetpack(bool isP2);
    int getTrail(bool isP2);
    int getShipTrail(bool isP2);
    int getDeathEffect(bool isP2);
    bool getDeathExplode(bool isP2);
    int getColor1(bool isP2);
    int getColor2(bool isP2);
    bool getGlow(bool isP2);
    int getGlowColor(bool isP2);
    cocos2d::CCMotionStreak* getShipFireNode(bool isP2);

    template <typename T>
    T getSaved(char const* key, T def) {
        return geode::Mod::get()->getSavedValue<T>(key, def);
    }

    template <typename T>
    void setSaved(char const* key, T value) {
        geode::Mod::get()->setSavedValue<T>(key, value);
    }

    bool m_insideCreatePlayer = false;
    bool m_isP2Main = false;
    bool m_shouldSwap = true;
    geode::WeakRef<cocos2d::CCMotionStreak> m_p1ShipFire = nullptr;
    geode::WeakRef<cocos2d::CCMotionStreak> m_p2ShipFire = nullptr;

private:
    Helper() = default;

    // First run: mirror the P1 kit into the P2 slots so dual mode looks normal
    // until the user customizes P2 in the garage.
    void ensureSeeded();
    bool m_seeded = false;
};

} // namespace paimon::separate_dual
