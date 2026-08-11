#pragma once

// Per level progress the game throws away: where you die, how often, and how
// normal runs compare to practice runs.
//
// Deaths are kept as one counter per percent (0..100) instead of a list of
// events. That is all the heatmap needs, it cannot grow without bound, and it
// survives thousands of attempts in a few hundred bytes per level.
//
// Lives in its own file (info_progress.json) because it is written far more
// often than the rest of InfoStore.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::info {

constexpr int kPercentBuckets = 101;  // 0..100 inclusive
constexpr int kMaxRuns = 48;          // recent attempts kept for the jump chart

// One finished attempt: how many jumps it took and how far it got.
struct RunRecord {
    uint16_t jumps = 0;
    uint8_t percent = 0;
    bool practice = false;
};

// Percent that killed you the most, and how many times it did.
struct DeathPeak {
    int percent = -1;   // -1 while the level has no deaths recorded
    uint32_t count = 0;
};

struct LevelProgress {
    int attempts = 0;
    int practiceAttempts = 0;
    int completions = 0;
    int bestNormal = 0;
    int bestPractice = 0;
    int jumpsNormal = 0;
    int jumpsPractice = 0;
    int64_t playSeconds = 0;
    int64_t lastPlayed = 0;
    std::array<uint32_t, kPercentBuckets> deathsNormal{};
    std::array<uint32_t, kPercentBuckets> deathsPractice{};
    std::vector<RunRecord> runs;  // oldest first, capped at kMaxRuns

    int totalDeaths(bool practice) const;
    // Worst percent together with its death count: the count is what tells a
    // real wall apart from a percent that happens to lead a tie of ones.
    DeathPeak worstDeath(bool practice) const;
    int jumps(bool practice) const { return practice ? jumpsPractice : jumpsNormal; }
    // Recent attempts of one mode, oldest first.
    std::vector<RunRecord> recentRuns(bool practice) const;
};

class ProgressTracker {
public:
    static ProgressTracker& get();

    void recordDeath(int levelID, int percent, bool practice);
    void recordAttempt(int levelID, bool practice);
    void recordCompletion(int levelID, bool practice);
    void recordPlayTime(int levelID, int64_t seconds);
    void recordBest(int levelID, int percent, bool practice);
    void recordJump(int levelID, bool practice);
    // Closes an attempt: its jump count and the percent it ended on. Zero jumps
    // is a real answer, not a missing one, so those attempts are kept as well.
    void recordRun(int levelID, int jumps, int percent, bool practice);

    LevelProgress const* find(int levelID) const;
    bool hasData(int levelID) const;

    void save();

private:
    ProgressTracker() { load(); }
    void load();
    LevelProgress& touch(int levelID);
    void enforceLimit();

    std::unordered_map<int, LevelProgress> m_levels;
    bool m_dirty = false;
};

} // namespace paimon::info
