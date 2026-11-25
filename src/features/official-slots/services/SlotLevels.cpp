#include "../../../utils/FilesystemPath.hpp"
#include "SlotLevels.hpp"

#include "GmdImporter.hpp"
#include "OfficialSlotStore.hpp"
#include "SlotPayloadCache.hpp"
#include "SlotListRefresh.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::officialslots {

namespace {

std::uint64_t g_playRequest = 0;
std::optional<int> g_playReturnPage;
GJGameLevel* g_playingLevel = nullptr;

bool sameSource(Slot const& a, Slot const& b) {
    return a.source == b.source && a.levelId == b.levelId &&
           a.gmdFile == b.gmdFile && a.replacesOfficialId == b.replacesOfficialId;
}

std::string gdString(std::string const& str) {
    return str.empty() ? std::string{} : std::string(str.c_str());
}

bool levelStringEmpty(GJGameLevel* level) {
    return !level || level->m_levelString.empty();
}

void copyPlayableData(GJGameLevel* target, GJGameLevel* source) {
    target->m_levelString = source->m_levelString;
    target->m_audioTrack = source->m_audioTrack;
    target->m_songID = source->m_songID;
    target->m_songIDs = source->m_songIDs;
    target->m_sfxIDs = source->m_sfxIDs;
    target->m_levelLength = source->m_levelLength;
    target->m_levelNotDownloaded = levelStringEmpty(source);
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

SlotDownloads& SlotDownloads::get() {
    static SlotDownloads instance;
    return instance;
}

void SlotDownloads::fetch(int levelId, FetchCallback callback) {
    if (levelId <= 0) {
        callback(nullptr);
        return;
    }

    if (auto* cached = this->cachedLevel(levelId)) {
        callback(cached);
        return;
    }
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(levelId)) {
            if (!levelStringEmpty(saved)) {
                this->rememberLevel(saved, true);
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

    m_pending = std::move(m_queue.front());
    m_queue.erase(m_queue.begin());
    int const levelId = m_pending->levelId;
    auto const directory = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "official-slot-levels";
    // disk and JSON work never block a scrolling page or start its music early.
    bool const spawned = paimon::ThreadTracker::get().spawn([this, directory, levelId] {
        auto payload = readSlotPayload(directory, levelId);
        if (paimon::isRuntimeShuttingDown()) return;
        Loader::get()->queueInMainThread([this, levelId, payload = std::move(payload)] {
            if (paimon::isRuntimeShuttingDown() || !m_pending || m_pending->levelId != levelId) return;
            if (!payload) {
                this->requestNetwork();
                return;
            }
            // Cocos objects are created and retained only on the main thread.
            auto* level = GJGameLevel::create();
            if (!level) {
                this->finishPending(nullptr);
                return;
            }
            level->m_levelID = levelId;
            level->m_levelString = payload->levelString.c_str();
            level->m_audioTrack = payload->audioTrack;
            level->m_songID = payload->songId;
            level->m_songIDs = payload->songIds.c_str();
            level->m_sfxIDs = payload->sfxIds.c_str();
            level->m_levelLength = payload->levelLength;
            level->m_levelName = payload->name.c_str();
            level->m_creatorName = payload->author.c_str();
            level->m_difficulty = static_cast<GJDifficulty>(payload->difficulty);
            level->m_ratings = payload->ratings;
            level->m_ratingsSum = payload->ratingsSum;
            level->m_demon = payload->demon;
            level->m_demonDifficulty = payload->demonDifficulty;
            level->m_autoLevel = payload->autoLevel;
            level->m_stars = payload->stars;
            level->m_coins = payload->coins;
            level->m_featured = payload->featured;
            level->m_isEpic = payload->epic;
            level->m_levelNotDownloaded = false;
            this->rememberLevel(level, false);
            this->finishPending(level);
        });
    });
    if (!spawned) this->finishPending(nullptr);
}

void SlotDownloads::requestNetwork() {

    auto* glm = GameLevelManager::get();
    if (!glm) {
        this->finishPending(nullptr);
        return;
    }

    // borrow the single download delegate slot; vanilla overwrites it back, ours resolves on its own result.
    m_previous = glm->m_levelDownloadDelegate;
    glm->m_levelDownloadDelegate = this;
    glm->downloadLevel(m_pending->levelId, false, 0);
}

GJGameLevel* SlotDownloads::cachedLevel(int levelId) const {
    auto it = m_cached.find(levelId);
    return it == m_cached.end() ? nullptr : it->second.data();
}

void SlotDownloads::rememberLevel(GJGameLevel* level, bool persist) {
    if (levelStringEmpty(level) || level->m_levelID.value() <= 0) return;
    int const id = level->m_levelID.value();
    if (this->cachedLevel(id) == level) return;
    m_cached[id] = level;
    if (!persist) return;
    CachedSlotPayload payload;
    payload.levelId = id;
    payload.levelString = gdString(level->m_levelString);
    payload.audioTrack = level->m_audioTrack;
    payload.songId = level->m_songID;
    payload.songIds = gdString(level->m_songIDs);
    payload.sfxIds = gdString(level->m_sfxIDs);
    payload.levelLength = level->m_levelLength;
    payload.name = gdString(level->m_levelName);
    payload.author = gdString(level->m_creatorName);
    payload.difficulty = static_cast<int>(level->m_difficulty);
    payload.ratings = level->m_ratings;
    payload.ratingsSum = level->m_ratingsSum;
    payload.demon = level->m_demon.value();
    payload.demonDifficulty = level->m_demonDifficulty;
    payload.autoLevel = level->m_autoLevel;
    payload.stars = level->m_stars.value();
    payload.coins = level->m_coins;
    payload.featured = level->m_featured;
    payload.epic = level->m_isEpic;
    auto const directory = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "official-slot-levels";
    paimon::ThreadTracker::get().spawn([directory, payload = std::move(payload)] {
        if (!writeSlotPayload(directory, payload)) {
            log::warn("[OfficialSlots] Could not persist level {}", payload.levelId);
        }
    });
}

void SlotDownloads::finishPending(GJGameLevel* level) {
    if (!m_pending) return;
    if (!levelStringEmpty(level)) this->rememberLevel(level, true);
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
    if (!m_pending) return;
    // a vanilla request that started before we borrowed the delegate: ignore it
    // and keep waiting for ours (m_previous has no ownership, never call it back).
    if (!level || level->m_levelID != m_pending->levelId) return;
    this->finishPending(level);
}

void SlotDownloads::levelDownloadFailed(int) {
    // the failure carries no level id, so it may be ours or a vanilla one
    // that raced us. fail our fetch — the user can retry with one tap.
    if (m_pending) this->finishPending(nullptr);
}

SlotLevelCache& SlotLevelCache::get() {
    static SlotLevelCache instance;
    return instance;
}

GJGameLevel* SlotLevelCache::levelForSlot(Slot const& slot) {
    auto it = m_levels.find(slot.id);
    if (it != m_levels.end() && it->second.level) {
        if (sameSource(it->second.snapshot, slot)) {
            applyDisplayFields(it->second.level, slot);
            it->second.snapshot = slot;
            return it->second.level;
        }
        this->invalidate(slot.id);
    }

    // LevelPage renders IDs <= 0 as coming-soon pages. Use positive local IDs
    // outside the official range; downloads always use slot.levelId instead.
    int const fakeId = 1'000'000'000 + static_cast<int>(m_nextFakeId++ % 1'000'000u);
    auto* level = this->build(slot, fakeId);
    if (!level) return nullptr;

    m_levels.emplace(slot.id, Entry{Ref<GJGameLevel>(level), slot});
    return level;
}

int pageSourceLevelId(GJGameLevel* level) {
    if (!level) return 0;
    int const id = level->m_levelID.value();
    if (!paimon::modules::isEnabled(kModuleId)) return id;
    std::optional<Slot> slot;
    if (auto slotId = SlotLevelCache::get().slotIdForLevel(level)) {
        slot = SlotStore::get().find(*slotId);
        if (!slot) return 0;
    } else if (isOfficialId(id)) {
        slot = SlotStore::get().slotForOfficial(id);
    }
    if (!slot) return id;
    return slot->source == Source::LevelId ? slot->levelId : 0;
}

std::optional<std::string> SlotLevelCache::slotIdForLevel(GJGameLevel const* level) const {
    if (!level) return std::nullopt;
    for (auto const& [slotId, entry] : m_levels) {
        if (entry.level.data() == level) return slotId;
    }
    return std::nullopt;
}

void SlotLevelCache::invalidate() {
    m_levels.clear();
}

void SlotLevelCache::invalidate(std::string const& slotId) {
    m_levels.erase(slotId);
}

GJGameLevel* SlotLevelCache::build(Slot const& slot, int fakeId) {
    auto* level = GJGameLevel::create();
    if (!level) return nullptr;

    // local and unrated on purpose: with 0 stars/coins there is no reward
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

    // a replacement keeps the official page's song and length so the row
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
        auto data = readGmd(path);
        if (data.info) {
            level->m_songID = data.info->songId;
            level->m_audioTrack = data.info->songId > 0 ? 0 : data.info->audioTrack;
        }
        level->m_levelString = data.levelString.c_str();
        level->m_levelNotDownloaded = data.levelString.empty();
    } else if (slot.source == Source::LevelId && slot.levelId > 0) {
        // reuse source data independently of page edits and replacement IDs.
        if (auto* saved = SlotDownloads::get().cachedLevel(slot.levelId)) {
            copyPlayableData(level, saved);
            if (slot.name.empty()) level->m_levelName = saved->m_levelName.c_str();
            if (slot.author.empty()) level->m_creatorName = saved->m_creatorName.c_str();
        } else if (auto* glm = GameLevelManager::get()) {
            if (auto* saved = glm->getSavedLevel(slot.levelId)) {
                std::string str = gdString(saved->m_levelString);
                if (!str.empty()) {
                    // migrate previously saved slots into our persistent source
                    // cache even when vanilla already has the level in memory.
                    SlotDownloads::get().fetch(slot.levelId, [](GJGameLevel*) {});
                    copyPlayableData(level, saved);
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

void loadSlotLevel(Slot const& slot, SlotReadyCallback callback) {
    auto* level = SlotLevelCache::get().levelForSlot(slot);
    if (!levelStringEmpty(level)) {
        callback(level);
        return;
    }
    if (!level || slot.source != Source::LevelId || slot.levelId <= 0) {
        callback(nullptr);
        return;
    }
    SlotDownloads::get().fetch(slot.levelId, [slot, callback = std::move(callback)](GJGameLevel* downloaded) {
        auto current = SlotStore::get().find(slot.id);
        if (!current || !sameSource(slot, *current) || levelStringEmpty(downloaded)) {
            callback(nullptr);
            return;
        }
        auto* fresh = SlotLevelCache::get().levelForSlot(*current);
        if (fresh) copyPlayableData(fresh, downloaded);
        callback(fresh);
    });
}

void cacheSlotLevel(Slot const& slot) {
    auto cacheSong = [](GJGameLevel* level) {
        if (!level || level->m_songID <= 0) return;
        ProfileMusicManager::get().downloadSongForPreview(level->m_songID, [](bool, std::string const&) {});
    };
    if (slot.source == Source::LevelId) {
        SlotDownloads::get().fetch(slot.levelId, cacheSong);
    } else {
        loadSlotLevel(slot, cacheSong);
    }
}

void openSlotLevel(Slot const& slot) {
    auto* director = CCDirector::get();
    if (!director || !director->getRunningScene() || AudioContextCoordinator::get().isGameplayActive()) return;
    auto const request = ++g_playRequest;
    WeakRef<CCScene> origin(director->getRunningScene());
    auto isCurrent = [slot, request, origin] {
        auto scene = origin.lock();
        if (!scene || request != g_playRequest || CCDirector::get()->getRunningScene() != scene.data()) return false;
        auto current = SlotStore::get().find(slot.id);
        return current && sameSource(slot, *current);
    };
    auto start = [isCurrent, origin](GJGameLevel* level) {
        if (!isCurrent()) return;
        auto& audio = AudioContextCoordinator::get();
        auto const previousContext = audio.getDynamicContextLayer();
        audio.prepareGameplayAudio(level);
        // LevelPage::playStep2 preloads music, then creates PlayLayer on the
        // next frame. Stop the preview first so its seek/filter cannot survive
        // into gameplay. loadMusic prepares the track without playing it.
        if (auto* engine = FMODAudioEngine::sharedEngine()) {
            engine->stopMusic(0);
            engine->loadMusic(level->getAudioFileName(), 1.f, 0.f, 1.f, true, 0, 0, false);
        }
        paimon::scheduleMainThreadDelay(0.f, [isCurrent, origin, previousContext, held = Ref<GJGameLevel>(level)] {
            auto& audio = AudioContextCoordinator::get();
            if (!isCurrent()) {
                // a layer entered meanwhile owns its own audio context.
                if (audio.getDynamicContextLayer() == DynSongLayer::None) {
                    audio.abortGameplayTransition(previousContext);
                    auto scene = origin.lock();
                    if (!scene || CCDirector::get()->getRunningScene() != scene.data()) {
                        audio.deactivateLevelSelect(true);
                    }
                }
                return;
            }
            int returnPage = 0;
            if (auto scene = origin.lock()) {
                if (auto* selector = scene->getChildByType<LevelSelectLayer>(0)) {
                    if (auto* scroll = selector->m_scrollLayer) {
                        returnPage = scroll->getRelativePageForNum(scroll->m_page);
                    }
                }
            }
            auto* gm = GameManager::get();
            int const previousScene = gm->m_sceneEnum;
            gm->m_sceneEnum = 9; // Same return destination as native LevelPage::playStep3.
            auto* scene = PlayLayer::scene(held, false);
            if (!scene) {
                gm->m_sceneEnum = previousScene;
                audio.abortGameplayTransition(previousContext);
                audio.activateLevelSelectLevel(held);
                PaimonNotify::show(Localization::get().getString("slot.play.failed"), NotificationIcon::Error);
                return;
            }
            // native playStep3 replaces the scene. A retained selector keeps
            // entry/exit transforms and stale music state when popped later.
            g_playReturnPage = returnPage;
            g_playingLevel = held;
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
        });
    };

    auto* cached = SlotLevelCache::get().levelForSlot(slot);
    if (slot.source == Source::LevelId && levelStringEmpty(cached)) {
        PaimonNotify::show(Localization::get().getString("slot.play.downloading"), NotificationIcon::Info);
    }
    loadSlotLevel(slot, [isCurrent, start](GJGameLevel* level) {
        if (!isCurrent()) return;
        if (levelStringEmpty(level)) {
            PaimonNotify::show(Localization::get().getString("slot.play.failed"), NotificationIcon::Error);
            return;
        }
        int const songId = level->m_songID;
        if (songId <= 0) {
            start(level);
            return;
        }
        auto* music = MusicDownloadManager::sharedState();
        if (!music) {
            PaimonNotify::show(Localization::get().getString("slot.play.failed"), NotificationIcon::Error);
            return;
        }
        if (music->isSongDownloaded(songId)) {
            start(level);
            return;
        }
        PaimonNotify::show(Localization::get().getString("slot.play.downloading_song"), NotificationIcon::Info);
        ProfileMusicManager::get().downloadSongForPreview(songId,
            [isCurrent, start, held = Ref<GJGameLevel>(level)](bool success, std::string const&) {
                if (!isCurrent()) return;
                if (success) start(held);
                else PaimonNotify::show(Localization::get().getString("slot.play.failed"), NotificationIcon::Error);
            });
    });
}

bool returnFromSlotLevel(GJGameLevel* level) {
    if (!level || level != g_playingLevel) return false;
    int const page = g_playReturnPage.value_or(0);
    g_playReturnPage.reset();
    g_playingLevel = nullptr;
    auto* director = CCDirector::get();
    if (!director) return false;
    auto* scene = LevelSelectLayer::scene(0);
    if (!scene) return false;
    // native init interprets its index against the vanilla list. Restore the
    // visible index after our reordered/compacted catalogue has been built.
    selectVisiblePage(scene->getChildByType<LevelSelectLayer>(0), page);
    // this return owns the gameplay lock; LevelSelectLayer inherits onEnter,
    // so Geode does not install a selector-specific hook for that callback.
    AudioContextCoordinator::get().activateLevelSelect(0, false);
    director->replaceScene(CCTransitionFade::create(0.5f, scene));
    return true;
}

} // namespace paimon::officialslots
