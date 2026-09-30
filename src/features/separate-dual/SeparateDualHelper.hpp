#pragma once

// rewritten from the behavior spec; keys/ids stay compatible with shipped kits.

#include <Geode/Geode.hpp>
#include "../../core/modules/ModuleRegistry.hpp"

namespace paimon::separate_dual {

constexpr char const* kModuleId = "paimbnails.separatedual.global";

inline bool moduleEnabled() {
    return modules::isEnabled(kModuleId);
}

// which side of the dual pair a value belongs to.
enum class Side {
    Primary,
    Secondary,
};

// every customizable icon slot, in save order.
enum class IconSlot : int {
    Cube = 0,
    Ship,
    Ball,
    Bird,
    Dart,
    Robot,
    Spider,
    Swing,
    Jetpack,
    Trail,
    ShipFire,
    Death,
    Count,
};

// save keys match already-shipped versions: do not rename (on-disk schema).
namespace save_key {
constexpr char const* kSeeded = "sdi-seeded";
constexpr char const* kSidePicked = "2pselected";
constexpr char const* kLastType = "lasttype";
constexpr char const* kLastMode = "lastmode";
constexpr char const* kInk = "color1";
constexpr char const* kTrim = "color2";
constexpr char const* kHalo = "colorglow";
constexpr char const* kHaloOn = "glow";
constexpr char const* kBurst = "deathexplode";
} // namespace save_key

// lasttype codes stored alongside each garage pick (schema, keep values).
enum LastPicked : int64_t {
    kLastCube = 0,
    kLastShip = 1,
    kLastBall = 2,
    kLastUfo = 3,
    kLastWave = 4,
    kLastRobot = 5,
    kLastSpider = 6,
    kLastSwing = 7,
    kLastJetpack = 8,
    kLastDeath = 98,
    kLastTrail = 99,
    kLastShipFire = 101,
};

// flat kit image used to exchange the live game kit with the stored one.
struct KitSnapshot {
    int64_t icons[static_cast<int>(IconSlot::Count)] = {};
    int64_t ink = 0;
    int64_t trim = 0;
    int64_t halo = 0;
    bool haloOn = false;
    bool burst = false;
};

// look of one vanilla trail id (gd parameters, stored as data).
struct TrailLook {
    float fade;
    float width;
    bool tintable;
    bool repeat;
    bool idle;
};

// frame animation of one ship-fire id (gd parameters, stored as data).
struct ExhaustAnim {
    float stepSeconds;
    int frames;
};

class DualKitVault {
public:
    static DualKitVault* get();

    // per-run state (cleared on level init/exit, or on non-practice reset).
    void resetRunState();
    void flipLead();
    bool leadIsSecondary() const;

    // which garage side is being edited.
    bool sideActiveIsSecondary() const;
    void chooseSide(bool secondary);

    // lets the playlayer death hook mute the exit-dual swap for one call.
    void setExitSwap(bool armed);
    bool exitSwapArmed() const;

    // lets the player-spawn hook know creations come from createplayer().
    void setSpawning(bool spawning);
    bool isSpawning() const;

    // first run: mirror the live kit so dual mode looks normal until player 2 is customized.
    void primeFromGame();

    // swap the live game kit with the stored second-player kit.
    void exchangeWithGame();

    // slot + color reads/writes (storage for secondary, live game for primary).
    int slotIcon(IconSlot slot, Side side);
    void storeSlot(IconSlot slot, int iconId);
    int inkOf(Side side);
    int trimOf(Side side);
    int haloOf(Side side);
    bool haloEnabled(Side side);
    void storeHaloEnabled(bool on);
    bool burstEnabled(Side side);
    void storeBurstEnabled(bool on);
    int slotIconForPreview(IconType type, Side side);

    // art + dressing.
    void ensureBurstArt(int id);
    void releaseBurstArt(int id);
    void dressFighter(PlayerObject* player, Side side);
    void dressDoll(SimplePlayer* player, IconType type, Side side);
    void fitTrail(PlayerObject* player, Side side);
    void fitShipExhaust(PlayerObject* player, Side side);
    char const* exhaustFrame(int exhaustId, float delta);
    cocos2d::CCMotionStreak* exhaustNode(Side side);

    // garage bookkeeping shared with the hooks.
    template <typename T>
    T load(char const* key, T fallback) {
        return geode::Mod::get()->getSavedValue<T>(key, fallback);
    }
    template <typename T>
    T load(char const* key, T fallback) const {
        return geode::Mod::get()->getSavedValue<T>(key, fallback);
    }
    template <typename T>
    void save(char const* key, T value) {
        geode::Mod::get()->setSavedValue<T>(key, value);
    }

    geode::WeakRef<cocos2d::CCMotionStreak> m_exhaustMain = nullptr;
    geode::WeakRef<cocos2d::CCMotionStreak> m_exhaustSecond = nullptr;

private:
    DualKitVault() = default;

    bool m_secondLeads = false;
    bool m_exitSwap = true;
    bool m_spawning = false;
    bool m_primed = false;
};

} // namespace paimon::separate_dual
