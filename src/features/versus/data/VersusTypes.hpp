#pragma once

#include <cstdint>
#include <string>

namespace paimon::versus {

// classic and platformer keep separate ratings, queues and histories: mixing
// them wrecks the matchmaking.
enum class Mode : uint8_t {
    Classic,
    Platformer,
};

enum class Format : uint8_t {
    Race,
    SuddenDeath,
    Attempts,
    TimeAttack,
    Ladder,
    Roulette,
    TugOfWar,
    KingOfTheHill,
    Relay,
    Friendly,
};

enum ModeMask : uint8_t {
    ModeClassic    = 1u << 0,
    ModePlatformer = 1u << 1,
    ModeAny        = ModeClassic | ModePlatformer,
};

enum class Rarity : uint8_t {
    Common,
    Rare,
    Epic,
    Legendary,
};

enum class CardTarget : uint8_t {
    Rival,
    Self,
    Both,
};

// order is the vs-card wire id: new cards go at the end, nothing is removed.
enum class CardId : uint8_t {
    Fog,
    Quake,
    Weight,
    Noise,
    Eye,
    Dice,
    Magnet,
    ZoomIn,
    ZoomOut,
    Mask,
    Lock,
    Checkpoint,
    Dispel,
    Bolt,
    Mirror,
    Freeze,
    Shield,
    Heart,
    Ghost,
    Hourglass,
    Reflect,
    Swap,
    Skull,
    Bomb,
    Count,
};

// the server drives up to countdown; the level drives the rest.
enum class Phase : uint8_t {
    Idle,
    Queued,
    Found,
    Banning,
    Loading,
    Countdown,
    Running,
    Finished,
};

enum class Outcome : uint8_t {
    Pending,
    Win,
    Loss,
    Draw,
    Void,
};

struct PlayerRef {
    int accountId = 0;
    int userId = 0;
    std::string name;
    int elo = 0;
    int wins = 0;
    int losses = 0;
    int placementsLeft = 0;
};

// live state of one side. both sides use the same struct so the hud can draw
// them with one function and the win checks read symmetrically.
struct SideState {
    float percent = 0.f;
    float bestPercent = 0.f;
    float finishTime = 0.f;
    int attempt = 1;
    int deaths = 0;
    bool alive = true;
    bool finished = false;
    bool forfeited = false; // walked out of the level, so the duel is handed over
    bool shielded = false;
    bool practice = false;  // practising, so the percent is parked rather than stopped
    bool spent = false;     // out of attempts, nothing more will be added
    uint8_t segments = 0;   // ladder: one bit per 25% claimed
};

char const* modeId(Mode mode);
char const* formatId(Format format);
char const* rarityId(Rarity rarity);

Mode modeFromId(std::string const& id);
Format formatFromId(std::string const& id);

} // namespace paimon::versus
