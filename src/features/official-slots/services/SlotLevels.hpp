#pragma once

// Stand-ins are local, unrated (0 stars/coins) and dontSave: never handed
// to currency code, so misses grant nothing.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots {

// One LevelDownloadDelegate at a time: a fetch briefly borrows
// GameLevelManager::m_levelDownloadDelegate and restores it on completion.
class SlotDownloads : public LevelDownloadDelegate {
public:
    static SlotDownloads& get();

    // Null level on failure (network error, deleted level, ...).
    using FetchCallback = std::function<void(GJGameLevel*)>;
    void fetch(int levelId, FetchCallback callback);

    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int response) override;

private:
    SlotDownloads() = default;

    void startNext();
    void finishPending(GJGameLevel* level);

    struct Pending {
        int levelId = 0;
        std::vector<FetchCallback> callbacks;
    };
    std::optional<Pending> m_pending;
    std::vector<Pending> m_queue;
    LevelDownloadDelegate* m_previous = nullptr;
};

// Cache keyed by slot id; rebuilt when the stored slot changes.
class SlotLevelCache {
public:
    static SlotLevelCache& get();

    // Null only when the slot has no playable content yet (a level id whose
    // download is still in flight). The returned level stays valid until
    // invalidate() is called.
    GJGameLevel* levelForSlot(Slot const& slot);

    void invalidate();
    void invalidate(std::string const& slotId);

private:
    SlotLevelCache() = default;

    GJGameLevel* build(Slot const& slot, int fakeId);

    std::unordered_map<std::string, geode::Ref<GJGameLevel>> m_levels;
    std::unordered_map<std::string, Slot> m_snapshot;
    int m_nextFakeId = 0;
};

// Undownloaded level-id slots show the game's spinner; GMD slots without
// level data toast and stay cosmetic-only.
void openSlotLevel(Slot const& slot);

} // namespace paimon::officialslots
