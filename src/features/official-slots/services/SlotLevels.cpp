#include "SlotLevels.hpp"

#include "GmdImporter.hpp"
#include "OfficialSlotStore.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::officialslots {

namespace {

bool sameSource(Slot const& a, Slot const& b) {
    return a.source == b.source && a.levelId == b.levelId &&
           a.gmdFile == b.gmdFile && a.replacesOfficialId == b.replacesOfficialId;
}

std::string gdString(gd::string const& str) {
    return str.empty() ? std::string{} : std::string(str.c_str());
}

bool levelStringEmpty(GJGameLevel* level) {
    return !level || gdString(level->m_levelString).empty();
}

void applyDisplayFields(GJGameLevel* level, Slot const& slot) {
    std::string name = slot.name;
    if (name.empty()) {
        name = slot.source == Source::LevelId && slot.levelId > 0
            ? fmt::format("Level #{}", slot.levelId)
            : Localization::get().getString("slot.level.unnamed");
    }
    level->m_levelName = name.c_str();
    level->m_creatorName = slot.author.empty() ? "-" : slot.author.c_str();
}

} // namespace

std::vector<VisibleEntry> visibleEntries() {
    auto& store = SlotStore::get();
    std::vector<VisibleEntry> entries;

    for (int id = 1; id <= 22; ++id) {
        if (store.isOfficialHidden(id)) continue;
        if (auto slot = store.slotForOfficial(id)) {
            entries.push_back({true, id, std::move(*slot)});
        } else {
            entries.push_back({false, id, {}});
        }
    }

    for (auto const& slot : store.slots()) {
        if (!slot.enabled) continue;
        if (slot.replacesOfficialId != 0) continue;
        entries.push_back({true, 0, slot});
    }
    return entries;
}

SlotDownloads& SlotDownloads::get() {
    static SlotDownloads instance;
    return instance;
}

void SlotDownloads::fetch(int levelId, FetchCallback callback) {
    if (levelId <= 0) {
        callback(nullptr);
        return;
    }

    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(levelId)) {
            if (!levelStringEmpty(saved)) {
                callback(saved);
                return;
            }
        }
    }

    if (m_pending && m_pending->levelId == levelId) {
        m_pending->callbacks.push_back(std::move(callback));
        return;
    }
    for (auto& queued : m_queue) {
        if (queued.levelId == levelId) {
            queued.callbacks.push_back(std::move(callback));
            return;
        }
    }
    m_queue.push_back({levelId, {std::move(callback)}});
    this->startNext();
}

void SlotDownloads::startNext() {
    if (m_pending || m_queue.empty()) return;

    auto* glm = GameLevelManager::get();
    if (!glm) {
        auto dropped = std::move(m_queue);
        m_queue.clear();
        for (auto& entry : dropped) {
            for (auto& callback : entry.callbacks) callback(nullptr);
        }
        return;
    }

    m_pending = std::move(m_queue.front());
    m_queue.erase(m_queue.begin());

    // Borrow the single download delegate slot; vanilla layers overwrite it
    // again for their own requests, which is fine — our timeout-free fetch
    // just resolves whenever its own result arrives or fails.
    m_previous = glm->m_levelDownloadDelegate;
    glm->m_levelDownloadDelegate = this;
    glm->downloadLevel(m_pending->levelId, false, 0);
}

void SlotDownloads::finishPending(GJGameLevel* level) {
    if (auto* glm = GameLevelManager::get()) {
        if (glm->m_levelDownloadDelegate == this) {
            glm->m_levelDownloadDelegate = m_previous;
        }
    }
    m_previous = nullptr;

    auto pending = std::move(*m_pending);
    m_pending.reset();
    this->startNext();
    for (auto& callback : pending.callbacks) callback(level);
}

void SlotDownloads::levelDownloadFinished(GJGameLevel* level) {
    if (!m_pending) {
        if (m_previous) m_previous->levelDownloadFinished(level);
        return;
    }
    // A vanilla request that started before we borrowed the delegate: hand it
    // back and keep waiting for ours.
    if (!level || level->m_levelID != m_pending->levelId) {
        if (m_previous) m_previous->levelDownloadFinished(level);
        return;
    }
    this->finishPending(level);
}

void SlotDownloads::levelDownloadFailed(int response) {
    // The failure carries no level id, so it may be ours or a vanilla one
    // that raced us. Forward it so vanilla still shows its own error, and
    // fail our fetch — the user can retry with one tap.
    if (m_previous) m_previous->levelDownloadFailed(response);
    if (m_pending) this->finishPending(nullptr);
}

SlotLevelCache& SlotLevelCache::get() {
    static SlotLevelCache instance;
    return instance;
}

GJGameLevel* SlotLevelCache::levelForSlot(Slot const& slot) {
    auto it = m_levels.find(slot.id);
    if (it != m_levels.end() && it->second) {
        auto snapshot = m_snapshot.find(slot.id);
        if (snapshot != m_snapshot.end() && sameSource(snapshot->second, slot)) {
            applyDisplayFields(it->second, slot);
            snapshot->second = slot;
            return it->second;
        }
        this->invalidate(slot.id);
    }

    int const fakeId = -(1000 + (m_nextFakeId++ % 8000));
    auto* level = this->build(slot, fakeId);
    if (!level) return nullptr;

    m_levels.emplace(slot.id, Ref<GJGameLevel>(level));
    m_reverse.emplace(level, slot.id);
    m_snapshot.emplace(slot.id, slot);
    return level;
}

std::optional<Slot> SlotLevelCache::slotForLevel(GJGameLevel* level) const {
    if (!level) return std::nullopt;
    auto it = m_reverse.find(level);
    if (it == m_reverse.end()) return std::nullopt;
    // The store is fresher than our snapshot (rename without rebuild).
    if (auto fresh = SlotStore::get().find(it->second)) return fresh;
    auto snapshot = m_snapshot.find(it->second);
    if (snapshot == m_snapshot.end()) return std::nullopt;
    return snapshot->second;
}

bool SlotLevelCache::isSlotLevel(GJGameLevel* level) const {
    return level && m_reverse.contains(level);
}

void SlotLevelCache::invalidate() {
    m_levels.clear();
    m_reverse.clear();
    m_snapshot.clear();
}

void SlotLevelCache::invalidate(std::string const& slotId) {
    auto it = m_levels.find(slotId);
    if (it != m_levels.end()) {
        m_reverse.erase(it->second.data());
        m_levels.erase(it);
    }
    m_snapshot.erase(slotId);
}

GJGameLevel* SlotLevelCache::build(Slot const& slot, int fakeId) {
    auto* level = GJGameLevel::create();
    if (!level) return nullptr;

    // Local and unrated on purpose: with 0 stars/coins there is no reward
    // path in the game that can pay out for this level.
    level->m_levelID = fakeId;
    level->m_levelType = GJLevelType::Saved;
    level->m_localOrSaved = true;
    level->m_difficulty = GJDifficulty::NA;
    level->m_stars = 0;
    level->m_coins = 0;
    level->m_coinsVerified = 0;
    level->m_demon = 0;
    level->m_demonDifficulty = 0;
    level->m_autoLevel = false;
    level->m_featured = 0;
    level->m_isEpic = 0;
    level->m_downloads = 0;
    level->m_likes = 0;
    level->m_dislikes = 0;
    level->m_dontSave = true;
    level->m_isEditable = false;
    level->m_isUploaded = false;
    applyDisplayFields(level, slot);

    // A replacement keeps the official page's song and length so the row
    // still feels like it belongs to the list.
    if (isOfficialId(slot.replacesOfficialId)) {
        if (auto* glm = GameLevelManager::get()) {
            if (auto* vanilla = glm->getMainLevel(slot.replacesOfficialId, true)) {
                level->m_audioTrack = vanilla->m_audioTrack;
                level->m_songID = vanilla->m_songID;
                level->m_levelLength = vanilla->m_levelLength;
            }
        }
    }

    if (slot.source == Source::Gmd && !slot.gmdFile.empty()) {
        auto path = SlotStore::get().gmdDir() / slot.gmdFile;
        if (auto info = readGmdInfo(path)) {
            if (info->songId > 0) {
                level->m_songID = info->songId;
                level->m_audioTrack = 0;
            }
        }
        std::string str = readGmdLevelString(path);
        level->m_levelString = str.c_str();
        level->m_levelNotDownloaded = str.empty();
    } else if (slot.source == Source::LevelId && slot.levelId > 0) {
        // An already-downloaded copy opens instantly; otherwise the string
        // arrives through SlotDownloads when the player presses play.
        if (auto* glm = GameLevelManager::get()) {
            if (auto* saved = glm->getSavedLevel(slot.levelId)) {
                std::string str = gdString(saved->m_levelString);
                if (!str.empty()) {
                    level->m_levelString = str.c_str();
                    if (slot.name.empty()) {
                        level->m_levelName = saved->m_levelName.c_str();
                    }
                    if (slot.author.empty()) {
                        level->m_creatorName = saved->m_creatorName.c_str();
                    }
                }
            }
        }
        level->m_levelNotDownloaded = levelStringEmpty(level);
    } else {
        level->m_levelNotDownloaded = true;
    }

    return level;
}

void openSlotLevel(Slot const& slot) {
    auto open = [](GJGameLevel* level) {
        auto* scene = LevelInfoLayer::scene(level, false);
        if (!scene) return;
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    };

    auto* level = SlotLevelCache::get().levelForSlot(slot);
    if (!level) {
        PaimonNotify::show(Localization::get().getString("slot.play.failed"),
                           NotificationIcon::Error);
        return;
    }
    if (!levelStringEmpty(level)) {
        open(level);
        return;
    }

    if (slot.source != Source::LevelId || slot.levelId <= 0) {
        PaimonNotify::show(Localization::get().getString("slot.play.no_data"),
                           NotificationIcon::Warning);
        return;
    }

    PaimonNotify::show(Localization::get().getString("slot.play.downloading"),
                       NotificationIcon::Info);
    std::string slotId = slot.id;
    SlotDownloads::get().fetch(slot.levelId, [slotId, open](GJGameLevel* downloaded) {
        if (!downloaded || levelStringEmpty(downloaded)) {
            PaimonNotify::show(Localization::get().getString("slot.play.failed"),
                               NotificationIcon::Error);
            return;
        }
        auto current = SlotStore::get().find(slotId);
        if (!current) return;
        auto* fresh = SlotLevelCache::get().levelForSlot(*current);
        if (!fresh || levelStringEmpty(fresh)) {
            // The download landed after our stand-in was built; copy the
            // string over instead of rebuilding the level mid-flight.
            if (fresh) {
                fresh->m_levelString = downloaded->m_levelString.c_str();
                fresh->m_levelNotDownloaded = false;
            } else {
                PaimonNotify::show(Localization::get().getString("slot.play.failed"),
                                   NotificationIcon::Error);
                return;
            }
        }
        open(fresh);
    });
}

} // namespace paimon::officialslots
