#pragma once

// Separate 2nd player's kit from your own.
// Idea inspired by Weebify's "Separate Dual Icons" mod, available on the
// Geode mod index (mods.geode-sdk.org).
//
// Rewritten for Paimbnails from the behavior spec (2026-09-20): same
// user-visible behavior, own architecture and code. Icon storage is a
// slot table instead of per-icon accessors, kit swaps go through a flat
// snapshot, and trail/ship-fire tuning comes from data tables. The on-disk
// save schema (key names) is intentionally preserved so existing
// second-player kits keep working, and the node IDs "player2-icon",
// "swap-2p-button" and "2p-toggler" are kept because icon-gradients reads
// them. No code from the original mod remains.

#include <Geode/Geode.hpp>
#include "../../core/modules/ModuleRegistry.hpp"

namespace paimon::separate_dual {

constexpr char const* kModuleId = "paimbnails.separatedual.global";

inline bool moduleEnabled() {
    return modules::isEnabled(kModuleId);
}

// Which side of the dual pair a value belongs to.
enum class Side {
    Primary,
    Secondary,
};

// Every customizable icon slot, in save order.
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

// Save keys shared with already-shipped versions: do not rename. The odd
// historical names ("roll" for ball, "bird" for ufo, ...) and the lasttype
// codes below are part of the on-disk schema, as is the "2pselected" flag
// that icon-gradients reads.
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

// Flat kit image used to exchange the live game kit with the stored one.
struct KitSnapshot {
    int64_t icons[static_cast<int>(IconSlot::Count)] = {};
    int64_t ink = 0;
    int64_t trim = 0;
    int64_t halo = 0;
    bool haloOn = false;
    bool burst = false;
};

// Look of one vanilla trail id (GD parameters, stored as data).
struct TrailLook {
    float fade;
    float width;
    bool tintable;
    bool repeat;
    bool idle;
};

// Frame animation of one ship-fire id (GD parameters, stored as data).
struct ExhaustAnim {
    float stepSeconds;
    int frames;
};

class DualKitVault {
public:
    static DualKitVault* get();

    // Per-run state (cleared on level init/exit, or on non-practice reset).
    void resetRunState();
    void flipLead();
    bool leadIsSecondary() const;

    // Which garage side is being edited.
    bool sideActiveIsSecondary() const;
    void chooseSide(bool secondary);

    // Lets the PlayLayer death hook mute the exit-dual swap for one call.
    void setExitSwap(bool armed);
    bool exitSwapArmed() const;

    // Lets the player-spawn hook know creations come from createPlayer().
    void setSpawning(bool spawning);
    bool isSpawning() const;

    // First run: mirror the live kit into storage so dual mode looks
    // normal until the user customizes the second player in the garage.
    void primeFromGame();

    // Swap the live game kit with the stored second-player kit.
    void exchangeWithGame();

    // Slot + color reads/writes (storage for Secondary, live game for Primary).
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

    // Art + dressing.
    void ensureBurstArt(int id);
    void releaseBurstArt(int id);
    void dressFighter(PlayerObject* player, Side side);
    void dressDoll(SimplePlayer* player, IconType type, Side side);
    void fitTrail(PlayerObject* player, Side side);
    void fitShipExhaust(PlayerObject* player, Side side);
    char const* exhaustFrame(int exhaustId, float delta);
    cocos2d::CCMotionStreak* exhaustNode(Side side);

    // Garage bookkeeping shared with the hooks.
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
