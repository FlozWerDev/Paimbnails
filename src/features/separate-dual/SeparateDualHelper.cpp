#include "SeparateDualHelper.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::separate_dual {

namespace {

// Storage row for one icon slot: save key plus first-run fallback.
struct SlotRow {
    IconSlot slot;
    char const* key;
    int64_t fallback;
};

constexpr SlotRow kSlotRows[] = {
    {IconSlot::Cube, "cube", 1},
    {IconSlot::Ship, "ship", 1},
    {IconSlot::Ball, "roll", 1},
    {IconSlot::Bird, "bird", 1},
    {IconSlot::Dart, "dart", 1},
    {IconSlot::Robot, "robot", 1},
    {IconSlot::Spider, "spider", 1},
    {IconSlot::Swing, "swing", 1},
    {IconSlot::Jetpack, "jetpack", 1},
    {IconSlot::Trail, "trail", 1},
    {IconSlot::ShipFire, "shiptrail", 1},
    {IconSlot::Death, "death", 1},
};

// Live-game read for one slot (GD getters, dispatched on our own enum).
int readLiveIcon(GameManager* gm, IconSlot slot) {
    switch (slot) {
        case IconSlot::Cube: return gm->getPlayerFrame();
        case IconSlot::Ship: return gm->getPlayerShip();
        case IconSlot::Ball: return gm->getPlayerBall();
        case IconSlot::Bird: return gm->getPlayerBird();
        case IconSlot::Dart: return gm->getPlayerDart();
        case IconSlot::Robot: return gm->getPlayerRobot();
        case IconSlot::Spider: return gm->getPlayerSpider();
        case IconSlot::Swing: return gm->getPlayerSwing();
        case IconSlot::Jetpack: return gm->getPlayerJetpack();
        case IconSlot::Trail: return gm->getPlayerStreak();
        case IconSlot::ShipFire: return gm->getPlayerShipFire();
        case IconSlot::Death: return gm->getPlayerDeathEffect();
        default: return 1;
    }
}

char const* keyOf(IconSlot slot) {
    for (auto const& row : kSlotRows) {
        if (row.slot == slot) return row.key;
    }
    return "cube";
}

int64_t fallbackOf(IconSlot slot) {
    for (auto const& row : kSlotRows) {
        if (row.slot == slot) return row.fallback;
    }
    return 1;
}

// Vanilla trail look per trail id 1..7 (GD tuning, kept as data).
constexpr TrailLook kTrailLooks[] = {
    {0.3f, 10.0f, true, false, false},
    {0.3f, 14.0f, false, false, false},
    {0.3f, 8.5f, true, false, false},
    {0.4f, 10.0f, true, false, false},
    {0.6f, 5.0f, true, false, true},
    {1.0f, 3.0f, true, true, true},
    {0.3f, 14.0f, false, false, false},
};

// Ship-fire animation per exhaust id 2..6 (GD tuning, kept as data).
constexpr ExhaustAnim kExhaustAnims[] = {
    {3.0f / 96.0f, 9},
    {3.0f / 96.0f, 10},
    {5.0f / 12.0f, 6},
    {0.05f, 16},
    {5.0f / 12.0f, 5},
};

} // namespace

DualKitVault* DualKitVault::get() {
    static DualKitVault instance;
    return &instance;
}

void DualKitVault::resetRunState() {
    m_secondLeads = false;
    m_exitSwap = true;
}

void DualKitVault::flipLead() {
    m_secondLeads = !m_secondLeads;
}

bool DualKitVault::leadIsSecondary() const {
    return m_secondLeads;
}

bool DualKitVault::sideActiveIsSecondary() const {
    return load<bool>(save_key::kSidePicked, false);
}

void DualKitVault::chooseSide(bool secondary) {
    save<bool>(save_key::kSidePicked, secondary);
}

void DualKitVault::setExitSwap(bool armed) {
    m_exitSwap = armed;
}

bool DualKitVault::exitSwapArmed() const {
    return m_exitSwap;
}

void DualKitVault::setSpawning(bool spawning) {
    m_spawning = spawning;
}

bool DualKitVault::isSpawning() const {
    return m_spawning;
}

void DualKitVault::primeFromGame() {
    if (m_primed) return;
    m_primed = true;
    if (Mod::get()->getSavedValue<bool>(save_key::kSeeded, false)) return;

    auto gm = GameManager::get();
    auto mod = Mod::get();
    for (auto const& row : kSlotRows) {
        mod->setSavedValue<int64_t>(row.key, readLiveIcon(gm, row.slot));
    }
    mod->setSavedValue<int64_t>(save_key::kInk, gm->getPlayerColor());
    mod->setSavedValue<int64_t>(save_key::kTrim, gm->getPlayerColor2());
    mod->setSavedValue<int64_t>(save_key::kHalo, gm->getPlayerGlowColor());
    mod->setSavedValue<bool>(save_key::kHaloOn, gm->getPlayerGlow());
    mod->setSavedValue<bool>(save_key::kBurst, gm->getGameVariable("0153"));
    mod->setSavedValue<bool>(save_key::kSeeded, true);
}

namespace {

KitSnapshot snapshotLive(GameManager* gm) {
    KitSnapshot snap;
    for (auto const& row : kSlotRows) {
        snap.icons[static_cast<int>(row.slot)] = readLiveIcon(gm, row.slot);
    }
    snap.ink = gm->getPlayerColor();
    snap.trim = gm->getPlayerColor2();
    snap.halo = gm->getPlayerGlowColor();
    snap.haloOn = gm->getPlayerGlow();
    snap.burst = gm->getGameVariable("0153");
    return snap;
}

KitSnapshot snapshotStored(DualKitVault* vault) {
    KitSnapshot snap;
    for (auto const& row : kSlotRows) {
        snap.icons[static_cast<int>(row.slot)] = vault->load<int64_t>(row.key, row.fallback);
    }
    snap.ink = vault->load<int64_t>(save_key::kInk, 0);
    snap.trim = vault->load<int64_t>(save_key::kTrim, 0);
    snap.halo = vault->load<int64_t>(save_key::kHalo, 0);
    snap.haloOn = vault->load<bool>(save_key::kHaloOn, false);
    snap.burst = vault->load<bool>(save_key::kBurst, false);
    return snap;
}

void pushSnapshot(GameManager* gm, KitSnapshot const& snap) {
    gm->setPlayerFrame(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Cube)]));
    gm->setPlayerShip(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Ship)]));
    gm->setPlayerBall(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Ball)]));
    gm->setPlayerBird(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Bird)]));
    gm->setPlayerDart(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Dart)]));
    gm->setPlayerRobot(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Robot)]));
    gm->setPlayerSpider(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Spider)]));
    gm->setPlayerSwing(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Swing)]));
    gm->setPlayerJetpack(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Jetpack)]));
    gm->setPlayerStreak(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Trail)]));
    gm->setPlayerShipStreak(static_cast<int>(snap.icons[static_cast<int>(IconSlot::ShipFire)]));
    gm->setPlayerDeathEffect(static_cast<int>(snap.icons[static_cast<int>(IconSlot::Death)]));
    gm->setPlayerColor(static_cast<int>(snap.ink));
    gm->setPlayerColor2(static_cast<int>(snap.trim));
    gm->setPlayerColor3(static_cast<int>(snap.halo));
    gm->setPlayerGlow(snap.haloOn);
    gm->setGameVariable("0153", snap.burst);
}

void stashSnapshot(DualKitVault* vault, KitSnapshot const& snap) {
    for (auto const& row : kSlotRows) {
        vault->save<int64_t>(row.key, snap.icons[static_cast<int>(row.slot)]);
    }
    vault->save<int64_t>(save_key::kInk, snap.ink);
    vault->save<int64_t>(save_key::kTrim, snap.trim);
    vault->save<int64_t>(save_key::kHalo, snap.halo);
    vault->save<bool>(save_key::kHaloOn, snap.haloOn);
    vault->save<bool>(save_key::kBurst, snap.burst);
}

} // namespace

void DualKitVault::exchangeWithGame() {
    primeFromGame();
    auto gm = GameManager::get();
    KitSnapshot live = snapshotLive(gm);
    pushSnapshot(gm, snapshotStored(this));
    stashSnapshot(this, live);
}

bool usesStored(bool secondLeads, Side side) {
    return secondLeads != (side == Side::Secondary);
}

int DualKitVault::slotIcon(IconSlot slot, Side side) {
    primeFromGame();
    if (usesStored(m_secondLeads, side)) {
        return static_cast<int>(load<int64_t>(keyOf(slot), fallbackOf(slot)));
    }
    return readLiveIcon(GameManager::get(), slot);
}

void DualKitVault::storeSlot(IconSlot slot, int iconId) {
    save<int64_t>(keyOf(slot), iconId);
}

int DualKitVault::inkOf(Side side) {
    primeFromGame();
    if (usesStored(m_secondLeads, side)) return static_cast<int>(load<int64_t>(save_key::kInk, 0));
    return GameManager::get()->getPlayerColor();
}

int DualKitVault::trimOf(Side side) {
    primeFromGame();
    if (usesStored(m_secondLeads, side)) return static_cast<int>(load<int64_t>(save_key::kTrim, 0));
    return GameManager::get()->getPlayerColor2();
}

int DualKitVault::haloOf(Side side) {
    primeFromGame();
    auto gm = GameManager::get();
    int64_t halo = usesStored(m_secondLeads, side)
        ? load<int64_t>(save_key::kHalo, 0)
        : gm->getPlayerGlowColor();
    // GD records -1 while the glow channel tracks color 2.
    if (halo == -1) {
        return usesStored(m_secondLeads, side)
            ? static_cast<int>(load<int64_t>(save_key::kTrim, 0))
            : gm->getPlayerColor2();
    }
    return static_cast<int>(halo);
}

bool DualKitVault::haloEnabled(Side side) {
    primeFromGame();
    if (usesStored(m_secondLeads, side)) return load<bool>(save_key::kHaloOn, false);
    return GameManager::get()->getPlayerGlow();
}

void DualKitVault::storeHaloEnabled(bool on) {
    save<bool>(save_key::kHaloOn, on);
}

bool DualKitVault::burstEnabled(Side side) {
    primeFromGame();
    if (usesStored(m_secondLeads, side)) return load<bool>(save_key::kBurst, false);
    return GameManager::get()->getGameVariable("0153");
}

void DualKitVault::storeBurstEnabled(bool on) {
    save<bool>(save_key::kBurst, on);
}

int DualKitVault::slotIconForPreview(IconType type, Side side) {
    switch (type) {
        case IconType::Cube: return slotIcon(IconSlot::Cube, side);
        case IconType::Ship: return slotIcon(IconSlot::Ship, side);
        case IconType::Ball: return slotIcon(IconSlot::Ball, side);
        case IconType::Ufo: return slotIcon(IconSlot::Bird, side);
        case IconType::Wave: return slotIcon(IconSlot::Dart, side);
        case IconType::Robot: return slotIcon(IconSlot::Robot, side);
        case IconType::Spider: return slotIcon(IconSlot::Spider, side);
        case IconType::Swing: return slotIcon(IconSlot::Swing, side);
        case IconType::Jetpack: return slotIcon(IconSlot::Jetpack, side);
        case IconType::Special: return slotIcon(IconSlot::Trail, side);
        case IconType::ShipFire: return slotIcon(IconSlot::ShipFire, side);
        case IconType::DeathEffect: return slotIcon(IconSlot::Death, side);
        default: return -1;
    }
}

void DualKitVault::ensureBurstArt(int id) {
    auto gm = GameManager::get();
    if (id < 1) id = 1;
    if (id != gm->m_loadedDeathEffect && id > 1) {
        CCTextureCache::sharedTextureCache()->addImage(
            CCString::createWithFormat("PlayerExplosion_%02d.png", id - 1)->getCString(), false);
        CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile(
            CCString::createWithFormat("PlayerExplosion_%02d.plist", id - 1)->getCString());
    }
}

void DualKitVault::releaseBurstArt(int id) {
    auto gm = GameManager::get();
    if (id < 1) id = 1;
    if (id != gm->m_loadedDeathEffect && id > 1) {
        CCTextureCache::sharedTextureCache()->removeTextureForKey(
            CCString::createWithFormat("PlayerExplosion_%02d.png", id - 1)->getCString());
    }
}

namespace {

void paintFormFrame(PlayerObject* player, IconSlot form, int iconId) {
    switch (form) {
        case IconSlot::Ship: player->updatePlayerShipFrame(iconId); break;
        case IconSlot::Ball: player->updatePlayerRollFrame(iconId); break;
        case IconSlot::Bird: player->updatePlayerBirdFrame(iconId); break;
        case IconSlot::Dart: player->updatePlayerDartFrame(iconId); break;
        case IconSlot::Robot: player->updatePlayerRobotFrame(iconId); break;
        case IconSlot::Spider: player->updatePlayerSpiderFrame(iconId); break;
        case IconSlot::Swing: player->updatePlayerSwingFrame(iconId); break;
        default: player->updatePlayerFrame(iconId); break;
    }
}

IconSlot currentForm(PlayerObject* player) {
    if (player->m_isShip) return IconSlot::Ship;
    if (player->m_isBall) return IconSlot::Ball;
    if (player->m_isBird) return IconSlot::Bird;
    if (player->m_isDart) return IconSlot::Dart;
    if (player->m_isRobot) return IconSlot::Robot;
    if (player->m_isSpider) return IconSlot::Spider;
    if (player->m_isSwing) return IconSlot::Swing;
    return IconSlot::Cube;
}

} // namespace

void DualKitVault::dressFighter(PlayerObject* player, Side side) {
    auto gm = GameManager::get();

    player->setColor(gm->colorForIdx(inkOf(side)));
    player->setSecondColor(gm->colorForIdx(trimOf(side)));
    player->m_originalMainColor = gm->colorForIdx(inkOf(side));
    player->m_originalSecondColor = gm->colorForIdx(trimOf(side));

    // Jetpack/ship/bird pair their vehicle frame with the cube frame.
    if (player->m_isShip && player->m_isPlatformer) {
        player->updatePlayerJetpackFrame(slotIcon(IconSlot::Jetpack, side));
        player->updatePlayerFrame(slotIcon(IconSlot::Cube, side));
    } else if (player->m_isShip) {
        player->updatePlayerShipFrame(slotIcon(IconSlot::Ship, side));
        player->updatePlayerFrame(slotIcon(IconSlot::Cube, side));
    } else if (player->m_isBird) {
        player->updatePlayerBirdFrame(slotIcon(IconSlot::Bird, side));
        player->updatePlayerFrame(slotIcon(IconSlot::Cube, side));
    } else {
        paintFormFrame(player, currentForm(player), slotIcon(currentForm(player), side));
    }

    player->toggleGhostEffect(player->m_ghostType);
    player->m_hasGlow = haloEnabled(side);
    player->enableCustomGlowColor(gm->colorForIdx(haloOf(side)));
    player->updatePlayerGlow();
    player->updateGlowColor();
    fitTrail(player, side);
    fitShipExhaust(player, side);
}

void DualKitVault::dressDoll(SimplePlayer* player, IconType type, Side side) {
    if (!player) return;
    // Trail and death previews stay vanilla on garage dolls.
    if (type == IconType::Special || type == IconType::DeathEffect) return;

    int iconId = slotIconForPreview(type, side);
    if (iconId < 0) return;

    auto gm = GameManager::get();
    player->updatePlayerFrame(iconId, type);
    player->setColors(gm->colorForIdx(inkOf(side)), gm->colorForIdx(trimOf(side)));
    player->enableCustomGlowColor(gm->colorForIdx(haloOf(side)));
    player->m_hasGlowOutline = haloEnabled(side);
    player->updateColors();
}

void DualKitVault::fitTrail(PlayerObject* player, Side side) {
    int trail = slotIcon(IconSlot::Trail, side);
    if (trail < 1 || trail > 7) trail = 1;
    TrailLook const& look = kTrailLooks[trail - 1];

    player->m_streakStrokeWidth = look.width;
    player->m_alwaysShowStreak = look.idle;
    player->m_disableStreakTint = !look.tintable;

    player->m_playerStreak = trail;
    player->m_regularTrail->initWithFade(
        look.fade, 5.0f, look.width, ccc3(255, 255, 255),
        CCString::createWithFormat("streak_%02d_001.png", trail)->getCString());
    if (look.repeat) {
        player->m_regularTrail->enableRepeatMode(0.1);
    }
    player->m_regularTrail->m_fMaxSeg = 50.0f;
    player->m_regularTrail->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    if (look.tintable) {
        player->m_regularTrail->tintWithColor(player->getSecondColor());
    }
    if (!look.idle) {
        player->m_regularTrail->stopStroke();
    }
}

void DualKitVault::fitShipExhaust(PlayerObject* player, Side side) {
    int exhaust = slotIcon(IconSlot::ShipFire, side);
    player->m_shipStreakType = static_cast<ShipStreak>(exhaust);

    if (exhaust <= 1) {
        player->m_shipStreak = nullptr;
        return;
    }
    CCTexture2D* art = CCTextureCache::get()->addImage(exhaustFrame(exhaust, 0), false);
    if (CCMotionStreak* node = exhaustNode(side)) {
        node->setTexture(art);
        player->m_shipStreak = node;
    }
}

char const* DualKitVault::exhaustFrame(int exhaustId, float delta) {
    if (exhaustId < 2 || exhaustId > 6) return "";
    ExhaustAnim const& anim = kExhaustAnims[exhaustId - 2];
    if (anim.frames == 0) return "";

    int step = static_cast<int>(floorf(delta / anim.stepSeconds));
    int frame = step % anim.frames + 1;
    return CCString::createWithFormat("shipfire%02d_%03d.png", exhaustId, frame)->getCString();
}

CCMotionStreak* DualKitVault::exhaustNode(Side side) {
    auto& node = usesStored(m_secondLeads, side) ? m_exhaustSecond : m_exhaustMain;
    return node.lock().data();
}

} // namespace paimon::separate_dual
