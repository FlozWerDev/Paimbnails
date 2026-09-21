#pragma once

// Playable stand-ins for the cosmetic official slots. A slot is never handed
// to the game's currency code: the GJGameLevel built here is local, unrated
// (0 stars, 0 coins) and marked dontSave. Painted separately by the hooks in
// hooks/OfficialSlotHooks.cpp and by ui/SlotVisuals.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots {

// Downloads an online level's full data (name, author, difficulty, stars,
// level string) for slots whose source is a level id.
//
// The game only notifies one LevelDownloadDelegate at a time, so a fetch
// briefly borrows GameLevelManager::m_levelDownloadDelegate and restores the
// previous one on completion. Callbacks for levels the player already
// downloaded resolve immediately without touching the delegate at all.
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

// Builds and caches the fake GJGameLevel behind each slot. The cache is keyed
// by slot id and rebuilt automatically when the stored slot changes.
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

// Opens the slot for play through a vanilla LevelInfoLayer. Level-id slots
// that were never downloaded show the game's own spinner while the string
// arrives; GMD slots whose file lost its level data get an error toast and
// stay cosmetic-only.
void openSlotLevel(Slot const& slot);

} // namespace paimon::officialslots
