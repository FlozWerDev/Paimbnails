#pragma once

// GameLevelManager takes a single delegate: lookups leave one at a time
// through a FIFO; results are cached, the UI repaints on revision().

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::twitch {

struct LevelBrief {
    int levelID = 0;
    std::string name;
    std::string author;
    int stars = 0;
    int difficulty = 0;  // value understood by GJDifficultySprite
    bool found = false;
    int length = 0;      // 0 tiny .. 4 XL, meaningless on platformer
    bool platformer = false;
    int filterDifficulty = 0;
};

class TwitchLevelBriefCache final : public LevelManagerDelegate {
public:
    static TwitchLevelBriefCache& get();

    // Cached entry, or nullptr while it is still unknown.
    LevelBrief const* peek(int levelID) const;

    // Queues a lookup if the ID is not cached yet.
    void request(int levelID);

    // resolved level, ready to open a LevelInfoLayer.
    GJGameLevel* peekLevel(int levelID) const;

    // like request(), but fires when the level is ready (nullptr if the
    // download failed). Cached levels invoke the callback at once.
    void fetch(int levelID, std::function<void(GJGameLevel*)> callback);

    // Drives the queue and drops stalled lookups; call it from the UI refresh.
    void tick();

    uint64_t revision() const { return m_revision; }

private:
    TwitchLevelBriefCache() = default;

    void pump();
    void store(GJGameLevel* level);
    void finish(bool found);
    void flushCallbacks(int levelID);

    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFailed(char const* key) override;
    void setupPageInfo(gd::string info, char const* key) override;

    std::unordered_map<int, LevelBrief> m_cache;
    std::unordered_map<int, geode::Ref<GJGameLevel>> m_levels;
    std::unordered_map<int, std::vector<std::function<void(GJGameLevel*)>>> m_callbacks;
    std::deque<int> m_pending;
    int m_inFlight = 0;
    int64_t m_inFlightSince = 0;
    uint64_t m_revision = 0;
};

} // namespace paimon::twitch
