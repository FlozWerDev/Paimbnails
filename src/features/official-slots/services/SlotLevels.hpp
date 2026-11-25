#pragma once

// stand-ins are local, unrated (0 stars/coins) and dontsave: never handed
// to currency code, so misses grant nothing.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::officialslots {

// one leveldownloaddelegate at a time: a fetch briefly borrows
// gamelevelmanager::m_leveldownloaddelegate and restores it on completion.
class SlotDownloads : public LevelDownloadDelegate {
public:
    static SlotDownloads& get();

    // null level on failure (network error, deleted level, ...).
    using FetchCallback = std::function<void(GJGameLevel*)>;
    void fetch(int levelId, FetchCallback callback);
    GJGameLevel* cachedLevel(int levelId) const;

    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int response) override;

private:
    SlotDownloads() = default;

    void startNext();
    void requestNetwork();
    void rememberLevel(GJGameLevel* level, bool persist);
    void finishPending(GJGameLevel* level);

    struct Pending {
        int levelId = 0;
        std::vector<FetchCallback> callbacks;
    };
    std::optional<Pending> m_pending;
    std::vector<Pending> m_queue;
    LevelDownloadDelegate* m_previous = nullptr;
    std::unordered_map<int, geode::Ref<GJGameLevel>> m_cached;
};

// cache keyed by slot id; rebuilt when the stored slot changes.
class SlotLevelCache {
public:
    static SlotLevelCache& get();

    // null only while the level id download is in flight; valid until invalidate().
    GJGameLevel* levelForSlot(Slot const& slot);

    // levelselect recycles its pages, so a page is only known by the level it shows.
    std::optional<std::string> slotIdForLevel(GJGameLevel const* level) const;

    void invalidate();
    void invalidate(std::string const& slotId);

private:
    SlotLevelCache() = default;

    GJGameLevel* build(Slot const& slot, int fakeId);

    struct Entry {
        geode::Ref<GJGameLevel> level;
        Slot snapshot;
    };
    std::unordered_map<std::string, Entry> m_levels;
    std::uint32_t m_nextFakeId = 0;
};

// resolve the user's payload into its local page, sharing any pending ID download.
using SlotReadyCallback = std::function<void(GJGameLevel*)>;
void loadSlotLevel(Slot const& slot, SlotReadyCallback callback);

// warm persistent level data and the main song as soon as the user saves a slot.
void cacheSlotLevel(Slot const& slot);

// start gameplay directly, downloading the level and its main song when needed.
void openSlotLevel(Slot const& slot);
// rebuild the selector on the same page after directly launched custom gameplay.
bool returnFromSlotLevel(GJGameLevel* level);

// resolve a page's source for thumbnails without ever requesting the stand-in's synthetic ID.
int pageSourceLevelId(GJGameLevel* level);

} // namespace paimon::officialslots
