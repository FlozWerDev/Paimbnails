#include "SeparateDualHelper.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::separate_dual {

namespace {

char const* kSeedFlag = "sdi-seeded";

int64_t savedInt(char const* key, int64_t fallback) {
    return Mod::get()->getSavedValue<int64_t>(key, fallback);
}

bool savedBool(char const* key, bool fallback) {
    return Mod::get()->getSavedValue<bool>(key, fallback);
}

struct Kit {
    int cube;
    int ship;
    int ball;
    int bird;
    int dart;
    int robot;
    int spider;
    int swing;
    int jetpack;
    int trail;
    int shipTrail;
    int death;
    bool deathExplode;
    int color1;
    int color2;
    int glowColor;
    bool glow;
};

} // namespace

Helper* Helper::get() {
    static Helper instance;
    return &instance;
}

void Helper::ensureSeeded() {
    if (m_seeded) return;
    m_seeded = true;
    if (Mod::get()->getSavedValue<bool>(kSeedFlag, false)) return;

    auto GM = GameManager::get();
    auto mod = Mod::get();
    mod->setSavedValue<int64_t>("cube", GM->getPlayerFrame());
    mod->setSavedValue<int64_t>("ship", GM->getPlayerShip());
    mod->setSavedValue<int64_t>("roll", GM->getPlayerBall());
    mod->setSavedValue<int64_t>("bird", GM->getPlayerBird());
    mod->setSavedValue<int64_t>("dart", GM->getPlayerDart());
    mod->setSavedValue<int64_t>("robot", GM->getPlayerRobot());
    mod->setSavedValue<int64_t>("spider", GM->getPlayerSpider());
    mod->setSavedValue<int64_t>("swing", GM->getPlayerSwing());
    mod->setSavedValue<int64_t>("jetpack", GM->getPlayerJetpack());
    mod->setSavedValue<int64_t>("trail", GM->getPlayerStreak());
    mod->setSavedValue<int64_t>("shiptrail", GM->getPlayerShipFire());
    mod->setSavedValue<int64_t>("death", GM->getPlayerDeathEffect());
    mod->setSavedValue<bool>("deathexplode", GM->getGameVariable("0153"));
    mod->setSavedValue<int64_t>("color1", GM->getPlayerColor());
    mod->setSavedValue<int64_t>("color2", GM->getPlayerColor2());
    mod->setSavedValue<int64_t>("colorglow", GM->getPlayerGlowColor());
    mod->setSavedValue<bool>("glow", GM->getPlayerGlow());
    mod->setSavedValue<bool>(kSeedFlag, true);
}

void Helper::reset() {
    m_isP2Main = false;
    m_shouldSwap = true;
}

void Helper::swapAll() {
    m_isP2Main = !m_isP2Main;
}

bool Helper::isP2Selected() {
    return getSaved<bool>("2pselected", false);
}

void Helper::setP2Selected(bool selected) {
    setSaved<bool>("2pselected", selected);
}

void Helper::swapSavedKitWithGame() {
    ensureSeeded();

    auto GM = GameManager::get();
    auto mod = Mod::get();

    Kit gameKit{
        GM->getPlayerFrame(),
        GM->getPlayerShip(),
        GM->getPlayerBall(),
        GM->getPlayerBird(),
        GM->getPlayerDart(),
        GM->getPlayerRobot(),
        GM->getPlayerSpider(),
        GM->getPlayerSwing(),
        GM->getPlayerJetpack(),
        GM->getPlayerStreak(),
        GM->getPlayerShipFire(),
        GM->getPlayerDeathEffect(),
        GM->getGameVariable("0153"),
        GM->getPlayerColor(),
        GM->getPlayerColor2(),
        GM->getPlayerGlowColor(),
        GM->getPlayerGlow()
    };

    Kit p2Kit{
        static_cast<int>(getSaved<int64_t>("cube", 1)),
        static_cast<int>(getSaved<int64_t>("ship", 1)),
        static_cast<int>(getSaved<int64_t>("roll", 1)),
        static_cast<int>(getSaved<int64_t>("bird", 1)),
        static_cast<int>(getSaved<int64_t>("dart", 1)),
        static_cast<int>(getSaved<int64_t>("robot", 1)),
        static_cast<int>(getSaved<int64_t>("spider", 1)),
        static_cast<int>(getSaved<int64_t>("swing", 1)),
        static_cast<int>(getSaved<int64_t>("jetpack", 1)),
        static_cast<int>(getSaved<int64_t>("trail", 1)),
        static_cast<int>(getSaved<int64_t>("shiptrail", 1)),
        static_cast<int>(getSaved<int64_t>("death", 1)),
        getSaved<bool>("deathexplode", false),
        static_cast<int>(getSaved<int64_t>("color1", 0)),
        static_cast<int>(getSaved<int64_t>("color2", 0)),
        static_cast<int>(getSaved<int64_t>("colorglow", 0)),
        getSaved<bool>("glow", false)
    };

    auto applyToGame = [GM](Kit const& kit) {
        GM->setPlayerFrame(kit.cube);
        GM->setPlayerShip(kit.ship);
        GM->setPlayerBall(kit.ball);
        GM->setPlayerBird(kit.bird);
        GM->setPlayerDart(kit.dart);
        GM->setPlayerRobot(kit.robot);
        GM->setPlayerSpider(kit.spider);
        GM->setPlayerSwing(kit.swing);
        GM->setPlayerJetpack(kit.jetpack);
        GM->setPlayerStreak(kit.trail);
        GM->setPlayerShipStreak(kit.shipTrail);
        GM->setPlayerDeathEffect(kit.death);
        GM->setGameVariable("0153", kit.deathExplode);
        GM->setPlayerColor(kit.color1);
        GM->setPlayerColor2(kit.color2);
        GM->setPlayerColor3(kit.glowColor);
        GM->setPlayerGlow(kit.glow);
    };

    applyToGame(p2Kit);

    mod->setSavedValue<int64_t>("cube", gameKit.cube);
    mod->setSavedValue<int64_t>("ship", gameKit.ship);
    mod->setSavedValue<int64_t>("roll", gameKit.ball);
    mod->setSavedValue<int64_t>("bird", gameKit.bird);
    mod->setSavedValue<int64_t>("dart", gameKit.dart);
    mod->setSavedValue<int64_t>("robot", gameKit.robot);
    mod->setSavedValue<int64_t>("spider", gameKit.spider);
    mod->setSavedValue<int64_t>("swing", gameKit.swing);
    mod->setSavedValue<int64_t>("jetpack", gameKit.jetpack);
    mod->setSavedValue<int64_t>("trail", gameKit.trail);
    mod->setSavedValue<int64_t>("shiptrail", gameKit.shipTrail);
    mod->setSavedValue<int64_t>("death", gameKit.death);
    mod->setSavedValue<bool>("deathexplode", gameKit.deathExplode);
    mod->setSavedValue<int64_t>("color1", gameKit.color1);
    mod->setSavedValue<int64_t>("color2", gameKit.color2);
    mod->setSavedValue<int64_t>("colorglow", gameKit.glowColor);
    mod->setSavedValue<bool>("glow", gameKit.glow);
}

void Helper::loadDeathTextures(int id) {
    auto GM = GameManager::get();
    if (id < 1) id = 1;

    if (id != GM->m_loadedDeathEffect && id > 1) {
        CCTextureCache::sharedTextureCache()->addImage(
            CCString::createWithFormat("PlayerExplosion_%02d.png", id - 1)->getCString(),
            false
        );
        CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile(
            CCString::createWithFormat("PlayerExplosion_%02d.plist", id - 1)->getCString()
        );
    }
}

void Helper::unloadDeathTextures(int id) {
    auto GM = GameManager::get();
    if (id < 1) id = 1;

    if (id != GM->m_loadedDeathEffect && id > 1) {
        CCTextureCache::sharedTextureCache()->removeTextureForKey(
            CCString::createWithFormat("PlayerExplosion_%02d.png", id - 1)->getCString()
        );
    }
}

void Helper::setPlayerInfo(PlayerObject* player, bool isP2) {
    auto GM = GameManager::get();

    player->setColor(GM->colorForIdx(this->getColor1(isP2)));
    player->setSecondColor(GM->colorForIdx(this->getColor2(isP2)));
    player->m_originalMainColor = GM->colorForIdx(this->getColor1(isP2));
    player->m_originalSecondColor = GM->colorForIdx(this->getColor2(isP2));
    if (player->m_isShip) {
        if (player->m_isPlatformer) {
            player->updatePlayerJetpackFrame(this->getJetpack(isP2));
            player->updatePlayerFrame(this->getCube(isP2));
        } else {
            player->updatePlayerShipFrame(this->getShip(isP2));
            player->updatePlayerFrame(this->getCube(isP2));
        }
    } else if (player->m_isBall) {
        player->updatePlayerRollFrame(this->getBall(isP2));
    } else if (player->m_isBird) {
        player->updatePlayerBirdFrame(this->getUFO(isP2));
        player->updatePlayerFrame(this->getCube(isP2));
    } else if (player->m_isDart) {
        player->updatePlayerDartFrame(this->getWave(isP2));
    } else if (player->m_isRobot) {
        player->updatePlayerRobotFrame(this->getRobot(isP2));
    } else if (player->m_isSpider) {
        player->updatePlayerSpiderFrame(this->getSpider(isP2));
    } else if (player->m_isSwing) {
        player->updatePlayerSwingFrame(this->getSwing(isP2));
    } else {
        player->updatePlayerFrame(this->getCube(isP2));
    }
    player->toggleGhostEffect(player->m_ghostType);
    player->m_hasGlow = this->getGlow(isP2);
    player->enableCustomGlowColor(GM->colorForIdx(this->getGlowColor(isP2)));
    player->updatePlayerGlow();
    player->updateGlowColor();
    setupNormalStreak(player, isP2);
    setupShipFire(player, isP2);
}

void Helper::setSimplePlayerInfo(SimplePlayer* player, IconType type, bool isP2) {
    if (!player) return;

    int iconID = this->getIconID(type, isP2);
    if (iconID < 0 || type == IconType::Special || type == IconType::DeathEffect) return;

    auto GM = GameManager::get();
    player->updatePlayerFrame(iconID, type);
    player->setColors(
        GM->colorForIdx(this->getColor1(isP2)),
        GM->colorForIdx(this->getColor2(isP2))
    );
    player->enableCustomGlowColor(GM->colorForIdx(this->getGlowColor(isP2)));
    player->m_hasGlowOutline = this->getGlow(isP2);
    player->updateColors();
}

void Helper::setupNormalStreak(PlayerObject* player, bool isP2) {
    int trail = this->getTrail(isP2);
    if (trail < 1 || trail > 7) trail = 1;

    player->m_streakStrokeWidth = 10.0;
    player->m_alwaysShowStreak = false;
    player->m_disableStreakTint = false;

    float streakFade = 0.3;
    float streakStroke = 10.0;
    switch (trail) {
        case 2:
        case 7:
            streakStroke = 14.0;
            player->m_disableStreakTint = true;
            player->m_streakStrokeWidth = 14.0;
            break;
        case 3:
            streakStroke = 8.5;
            player->m_streakStrokeWidth = 8.5;
            break;
        case 4:
            streakFade = 0.4;
            streakStroke = 10.0;
            break;
        case 5:
            streakFade = 0.6;
            streakStroke = 5.0;
            player->m_streakStrokeWidth = 5.0;
            player->m_alwaysShowStreak = true;
            break;
        case 6:
            streakFade = 1.0;
            streakStroke = 3.0;
            player->m_streakStrokeWidth = 3.0;
            player->m_alwaysShowStreak = true;
            break;
    }

    player->m_playerStreak = trail;
    player->m_regularTrail->initWithFade(
        streakFade, 5.0, streakStroke, ccc3(255, 255, 255),
        CCString::createWithFormat("streak_%02d_001.png", trail)->getCString()
    );
    if (trail == 6) {
        player->m_regularTrail->enableRepeatMode(0.1);
    }
    player->m_regularTrail->m_fMaxSeg = 50.0;
    player->m_regularTrail->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    if (!player->m_disableStreakTint) {
        player->m_regularTrail->tintWithColor(player->getSecondColor());
    }
    if (!player->m_alwaysShowStreak) {
        player->m_regularTrail->stopStroke();
    }
}

void Helper::setupShipFire(PlayerObject* player, bool isP2) {
    auto SDI = Helper::get();
    int shipTrail = this->getShipTrail(isP2);

    player->m_shipStreakType = static_cast<ShipStreak>(shipTrail);

    if (shipTrail > 1) {
        CCTexture2D* texture2d = CCTextureCache::get()->addImage(
            this->getFrameForStreak(shipTrail, 0), false
        );
        CCMotionStreak* p = SDI->getShipFireNode(isP2);
        if (p) {
            p->setTexture(texture2d);
            player->m_shipStreak = p;
        }
    } else {
        player->m_shipStreak = nullptr;
    }
}

char const* Helper::getFrameForStreak(int shipFire, float delta) {
    int spritesCount;
    float timeStep;
    if (shipFire == 4) {
        timeStep = 5.0 / 12;
    } else if (shipFire == 5) {
        timeStep = 0.05;
    } else if (shipFire == 6) {
        timeStep = 5.0 / 12;
    } else {
        timeStep = 3.0 / 96;
    }

    if (shipFire == 2) {
        spritesCount = 9;
    } else if (shipFire == 3) {
        spritesCount = 10;
    } else {
        if (shipFire == 4) spritesCount = 6;
        else if (shipFire == 5) spritesCount = 16;
        else if (shipFire == 6) spritesCount = 5;
        else spritesCount = 0;
    }

    if (spritesCount == 0) return "";

    int step = (int)floorf(delta / timeStep);
    int spriteStep = step % spritesCount + 1;

    return CCString::createWithFormat(
        "shipfire%02d_%03d.png", shipFire, spriteStep
    )->getCString();
}

int Helper::getIconID(IconType type, bool isP2) {
    switch (type) {
        case IconType::Cube: return this->getCube(isP2);
        case IconType::Ship: return this->getShip(isP2);
        case IconType::Ball: return this->getBall(isP2);
        case IconType::Ufo: return this->getUFO(isP2);
        case IconType::Wave: return this->getWave(isP2);
        case IconType::Robot: return this->getRobot(isP2);
        case IconType::Spider: return this->getSpider(isP2);
        case IconType::Swing: return this->getSwing(isP2);
        case IconType::Jetpack: return this->getJetpack(isP2);
        case IconType::Special: return this->getTrail(isP2);
        case IconType::ShipFire: return this->getShipTrail(isP2);
        case IconType::DeathEffect: return this->getDeathEffect(isP2);
        default: return -1;
    }
}

int Helper::getCube(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("cube", 1)
        : GameManager::get()->getPlayerFrame();
}

int Helper::getShip(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("ship", 1)
        : GameManager::get()->getPlayerShip();
}

int Helper::getBall(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("roll", 1)
        : GameManager::get()->getPlayerBall();
}

int Helper::getUFO(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("bird", 1)
        : GameManager::get()->getPlayerBird();
}

int Helper::getWave(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("dart", 1)
        : GameManager::get()->getPlayerDart();
}

int Helper::getRobot(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("robot", 1)
        : GameManager::get()->getPlayerRobot();
}

int Helper::getSpider(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("spider", 1)
        : GameManager::get()->getPlayerSpider();
}

int Helper::getSwing(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("swing", 1)
        : GameManager::get()->getPlayerSwing();
}

int Helper::getJetpack(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("jetpack", 1)
        : GameManager::get()->getPlayerJetpack();
}

int Helper::getTrail(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("trail", 1)
        : GameManager::get()->getPlayerStreak();
}

int Helper::getShipTrail(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("shiptrail", 1)
        : GameManager::get()->getPlayerShipFire();
}

int Helper::getDeathEffect(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("death", 1)
        : GameManager::get()->getPlayerDeathEffect();
}

bool Helper::getDeathExplode(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedBool("deathexplode", false)
        : GameManager::get()->getGameVariable("0153");
}

int Helper::getColor1(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("color1", 0)
        : GameManager::get()->getPlayerColor();
}

int Helper::getColor2(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedInt("color2", 0)
        : GameManager::get()->getPlayerColor2();
}

bool Helper::getGlow(bool isP2) {
    ensureSeeded();
    return m_isP2Main != isP2
        ? savedBool("glow", false)
        : GameManager::get()->getPlayerGlow();
}

int Helper::getGlowColor(bool isP2) {
    if (m_isP2Main != isP2) {
        ensureSeeded();
        int64_t colorglow = savedInt("colorglow", 0);
        // vanilla behavior when glow color is -1
        if (colorglow == -1) {
            return savedInt("color2", 0);
        }
        return (int)colorglow;
    } else {
        int colorglow = GameManager::get()->getPlayerGlowColor();
        if (colorglow == -1) {
            return GameManager::get()->getPlayerColor2();
        }
        return colorglow;
    }
}

CCMotionStreak* Helper::getShipFireNode(bool isP2) {
    auto& node = m_isP2Main != isP2 ? m_p2ShipFire : m_p1ShipFire;
    return node.lock().data();
}

} // namespace paimon::separate_dual
