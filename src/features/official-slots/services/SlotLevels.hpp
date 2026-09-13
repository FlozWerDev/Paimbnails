#pragma once

// Playable stand-ins for the cosmetic official slots.
//
// A slot is never handed to the game's currency code: the GJGameLevel built
// here is local, unrated (0 stars, 0 coins) and marked dontSave, so finishing
// it cannot grant stars, orbs, diamonds or official progress. The difficulty,
// stars and coins the player *sees* are painted separately by the hooks in
// hooks/OfficialSlotsHook.cpp and by ui/SlotVisuals. If any of those paint
// hooks ever miss, the worst case is a page showing "Unrated" — the failure
// direction is always towards granting nothing, never towards granting real
// rewards.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots {

// One row of the official list exactly as the player sees it.
struct VisibleEntry {
    bool isSlot = false;
    // Vanilla id (1..22). For appended slots this is 0; for a replacement it
    // is the official page the slot is drawn on.
    int officialId = 0;
    Slot slot;
};

// Officials minus the hidden ones (replacements flagged), then the enabled
// appended slots. This order is the page order of the scroll layer.
std::vector<VisibleEntry> visibleEntries();

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

    std::optional<Slot> slotForLevel(GJGameLevel* level) const;
    bool isSlotLevel(GJGameLevel* level) const;

    void invalidate();
    void invalidate(std::string const& slotId);

private:
    SlotLevelCache() = default;

    GJGameLevel* build(Slot const& slot, int fakeId);

    std::unordered_map<std::string, geode::Ref<GJGameLevel>> m_levels;
    std::unordered_map<GJGameLevel*, std::string> m_reverse;
    std::unordered_map<std::string, Slot> m_snapshot;
    int m_nextFakeId = 0;
};

// Opens the slot for play through a vanilla LevelInfoLayer. Level-id slots
// that were never downloaded show the game's own spinner while the string
// arrives; GMD slots whose file lost its level data get an error toast and
// stay cosmetic-only.
void openSlotLevel(Slot const& slot);

} // namespace paimon::officialslots
