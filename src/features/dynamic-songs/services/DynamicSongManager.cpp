#include "DynamicSongManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../framework/HookInterceptor.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <random>
#include <chrono>
#include <cmath>
#include <sstream>
#include <set>

using namespace geode::prelude;

// ─── Helper: FMOD::Channel* del ChannelGroup de musica de fondo ─────
static FMOD::Channel* getMainBgChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
        return channel;
    }
    if (!engine->m_backgroundMusicChannel) return nullptr;
    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

// ─── DynSongFadeNode: fade per-frame via CCScheduler ────────────────
// Registrado directamente con el scheduler (no necesita estar en scene tree).
// Una sola llamada a cancel() detiene todo.
class DynSongFadeNode : public cocos2d::CCNode {
public:
    static DynSongFadeNode* create() {
        auto* ret = new DynSongFadeNode();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void startFade(float fromVol, float toVol, float durationSec) {
        // Cancelar fade anterior si habia uno
        if (m_active) cancel();

        m_fromVol = fromVol;
        m_toVol = toVol;
        m_duration = std::max(durationSec, 0.016f);
        m_elapsed = 0.0f;
        m_active = true;

        auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(DynSongFadeNode::onFadeTick),
            this, 0.0f, kCCRepeatForever, 0.0f, false
        );
    }

    void cancel() {
        if (!m_active) return;
        m_active = false;
        auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
    }

    bool isActive() const { return m_active; }

private:
    float m_fromVol = 0.f, m_toVol = 0.f;
    float m_duration = 0.f, m_elapsed = 0.f;
    bool m_active = false;

    void onFadeTick(float dt) {
        if (!m_active) return;

        m_elapsed += dt;
        float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);

        // Ease-in-out cuadratico
        float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
        float vol = m_fromVol + (m_toVol - m_fromVol) * eT;

        DynamicSongManager::get()->setDynamicVolume(std::clamp(vol, 0.0f, 1.0f));

        if (t >= 1.0f) {
            m_active = false;
            auto* sched = cocos2d::CCDirector::sharedDirector()->getScheduler();
            sched->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
            DynamicSongManager::get()->onFadeComplete();
        }
    }
};

// ─── Singleton ──────────────────────────────────────────────────────
DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

DynamicSongManager::~DynamicSongManager() {
    if (paimon::isRuntimeShuttingDown()) {
        m_fadeNode = nullptr;
        m_streamPollNode = nullptr;
        return;
    }

    if (m_fadeNode) {
        m_fadeNode->cancel();
        m_fadeNode->release();
        m_fadeNode = nullptr;
    }

    stopStreamingPreview();

    if (m_streamPollNode) {
        m_streamPollNode->unscheduleAllSelectors();
        m_streamPollNode->release();
        m_streamPollNode = nullptr;
    }
}

// ─── Layer control ──────────────────────────────────────────────────
void DynamicSongManager::enterLayer(DynSongLayer layer) {
    m_currentLayer = layer;
}

void DynamicSongManager::exitLayer(DynSongLayer layer) {
    if (m_currentLayer == layer) {
        m_currentLayer = DynSongLayer::None;
    }
}

// ─── Fade helpers ───────────────────────────────────────────────────
float DynamicSongManager::getFadeDurationSec() const {
    if (Mod::get()->getSettingValue<bool>("profile-music-crossfade")) {
        return static_cast<float>(Mod::get()->getSettingValue<double>("profile-music-fade-duration"));
    }
    return 0.15f; // siempre fade, pero corto si crossfade esta desactivado
}

void DynamicSongManager::fadeVolume(float from, float to, float durationSec, PostFadeAction action) {
    if (!m_fadeNode) {
        m_fadeNode = DynSongFadeNode::create();
        m_fadeNode->retain();
    }
    m_postFadeAction = action;
    m_fadeNode->startFade(from, to, durationSec);
}

void DynamicSongManager::cancelFade() {
    if (m_fadeNode && m_fadeNode->isActive()) {
        m_fadeNode->cancel();
    }
    m_postFadeAction = PostFadeAction::None;
}

void DynamicSongManager::onFadeComplete() {
    auto* engine = FMODAudioEngine::sharedEngine();
    float targetVol = engine ? engine->m_musicVolume : 1.0f;

    switch (m_postFadeAction) {
    case PostFadeAction::PlayPending: {
        // Dip-fade completado: cargar cancion pendiente, fade in
        stopStreamingPreview();
        playOnMainChannel(m_pendingSongPath, 0.0f);
        applyRandomSeek();
        m_activeSongPath = m_pendingSongPath;
        m_pendingSongPath.clear();
        m_state = DynState::FadingIn;
        m_lastFadeCompleteTime = std::chrono::steady_clock::now();
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::RestoreMenu: {
        // Fade-out completado: cargar menu, set Idle, fade cosmetico del menu
        stopStreamingPreview();
        loadMenuTrack(0.0f);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        // Fade cosmetico del menu (estado ya es Idle, hooks dejan pasar)
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::Cleanup:
        // fadeOutForLevelStart completado
        stopStreamingPreview();
        m_state = DynState::Idle;
        break;

    case PostFadeAction::None:
        // Fade-in completado
        if (m_state == DynState::FadingIn) {
            m_state = DynState::Playing;
            m_lastFadeCompleteTime = std::chrono::steady_clock::now();
        }
        break;
    }

    m_postFadeAction = PostFadeAction::None;
}

// ─── Canal principal ────────────────────────────────────────────────
void DynamicSongManager::playOnMainChannel(const std::string& songPath, float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    s_selfPlayMusic = true;
    engine->playMusic(songPath, true, 0.0f, 0);
    s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }

    AudioContextCoordinator::get().claimDynamicAudio();
}

void DynamicSongManager::loadMenuTrack(float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    auto gm = GameManager::get();
    if (!engine || !gm) return;
    if (gm->getGameVariable("0122")) return;
    if (engine->m_musicVolume <= 0.0f) return;
    // No restaurar menu music si el audio del video esta activo
    if (paimon::isVideoAudioInteropActive()) return;

    std::string menuTrack = gm->getMenuMusicFile();
    s_selfPlayMusic = true;
    engine->playMusic(menuTrack, true, 0.0f, 0);
    s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }

    if (m_savedMenuPos > 0) {
        engine->setMusicTimeMS(m_savedMenuPos, true, 0);
        m_savedMenuPos = 0;
    }
}

// ─── Seek aleatorio ─────────────────────────────────────────────────
void DynamicSongManager::applyRandomSeek() {
    auto engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return;

    unsigned int lengthMs = 0;
    currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs > 10000) {
        unsigned int minStart = static_cast<unsigned int>(lengthMs * 0.15f);
        unsigned int maxStart = static_cast<unsigned int>(lengthMs * 0.85f);
        if (maxStart > minStart) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
            bgCh->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
        }
    }
}

// ─── Rotacion de canciones por nivel ────────────────────────────────
std::vector<std::string> DynamicSongManager::getAllSongPaths(GJGameLevel* level) {
    std::vector<std::string> paths;
    std::set<int> seenIds;
    auto mdm = MusicDownloadManager::sharedState();

    // Cancion principal
    if (level->m_songID > 0) {
        if (mdm->isSongDownloaded(level->m_songID)) {
            paths.push_back(mdm->pathForSong(level->m_songID));
            seenIds.insert(level->m_songID);
        }
    } else {
        std::string filename = LevelTools::getAudioFileName(level->m_audioTrack);
        std::string fullPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(filename.c_str(), false);
        if (fullPath.empty()) fullPath = filename;
        if (!fullPath.empty()) paths.push_back(fullPath);
    }

    // Canciones adicionales (m_songIDs comma-separated)
    std::string songIdsStr = level->m_songIDs;
    if (!songIdsStr.empty()) {
        std::stringstream ss(songIdsStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start == std::string::npos) continue;
            token = token.substr(start, end - start + 1);
            if (token.empty()) continue;

            auto songIdResult = geode::utils::numFromString<int>(token);
            if (!songIdResult) continue;
            int songId = songIdResult.unwrap();
            if (songId <= 0 || seenIds.count(songId)) continue;
            seenIds.insert(songId);

            if (mdm->isSongDownloaded(songId)) {
                paths.push_back(mdm->pathForSong(songId));
            }
        }
    }

    return paths;
}

std::string DynamicSongManager::getNextRotationSong(GJGameLevel* level) {
    auto allPaths = getAllSongPaths(level);
    if (allPaths.size() <= 1) {
        return allPaths.empty() ? "" : allPaths[0];
    }

    int levelId = level->m_levelID;
    auto it = m_songRotationCache.find(levelId);
    if (it == m_songRotationCache.end() || it->second.empty()) {
        if (m_songRotationCache.size() >= MAX_ROTATION_CACHE_LEVELS) {
            m_songRotationCache.erase(m_songRotationCache.begin());
        }
        m_songRotationCache[levelId] = allPaths;
        it = m_songRotationCache.find(levelId);
    }

    std::string nextSong = it->second.front();
    it->second.erase(it->second.begin());
    return nextSong;
}

// ─── playSong ───────────────────────────────────────────────────────
void DynamicSongManager::playSong(GJGameLevel* level) {
    // ── Guard chain (claw-code pattern: authorize → pre-check → execute) ──

    // Guard 1: feature enabled
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    // Guard 2: valid input
    if (!level) return;
    // Guard 3: valid layer
    if (!isInValidLayer()) return;
    // Guard 4: music not disabled by GD
    if (GameManager::get()->getGameVariable("0122")) return;
    // Guard 5: video audio not blocking
    if (paimon::isVideoAudioInteropActive()) return;
    // Guard 6: engine available
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) return;
    // Guard 7: cooldown — prevent rapid-fire from checkPageLoop (~200ms)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - m_lastPlaySongTime;
        if (elapsed < std::chrono::milliseconds(200)) return;
        m_lastPlaySongTime = now;
    }
    // Guard 8: HookInterceptor — extensible pre-play check
    {
        paimon::HookContext ctx;
        ctx.action = "dynamic-play";
        ctx.levelID = level->m_levelID.value();
        auto result = paimon::HookInterceptor::get().runPreHooks(ctx);
        if (!result.isAllowed()) return;
    }

    int levelId = level->m_levelID.value();
    float targetVol = engine->m_musicVolume;

    if (levelId != m_currentPlayingLevelID && (m_streamingPreview || isStreamingPreviewPending())) {
        cancelFade();
        stopStreamingPreview();
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
    }

    // Guard 9: same context already active — skip without complex verification
    if (isActive() && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        if (m_state == DynState::Playing || m_state == DynState::FadingIn) {
            paimon::setDynamicSongInteropActive(true);
            return;
        }
    }

    // Obtener cancion
    std::string songPath;
    if (m_state != DynState::Idle && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        songPath = m_activeSongPath; // reusar para retry
    } else {
        songPath = getNextRotationSong(level);
    }
    if (songPath.empty()) {
        // No hay cancion local — intentar streaming preview
        startStreamingPreview(level);
        return;
    }

    // Limpiar suspension si habia
    if (m_state == DynState::Suspended) {
        m_state = DynState::Idle;
    }

    cancelFade();

    m_activeSongPath = songPath;
    m_currentPlayingLevelID = levelId;
    paimon::setDynamicSongInteropActive(true);

    if (m_state == DynState::Idle) {
        // Primera cancion: guardar posicion menu, cargar, fade in
        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        playOnMainChannel(songPath, 0.0f);
        applyRandomSeek();
        m_state = DynState::FadingIn;
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
    } else {
        // Song-to-song: dip fade (bajar, cargar nueva, subir)
        float currentVol = getDynamicVolume();
        m_pendingSongPath = songPath;
        m_state = DynState::FadingOut;
        fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::PlayPending);
    }

    AudioContextCoordinator::get().claimDynamicAudio();
}

// ─── stopSong ───────────────────────────────────────────────────────
void DynamicSongManager::stopSong() {
    if (!isActive()) return;

    cancelFade();

    if (isStreamingPreviewPending() && !m_streamingPreview) {
        stopStreamingPreview();
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    if (m_state == DynState::Suspended) {
        stopStreamingPreview();
        // Sin audio activo, restaurar menu directo
        loadMenuTrack(FMODAudioEngine::sharedEngine()->m_musicVolume);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    // Limpiar ownership inmediato para que hooks dejen pasar musica del menu
    // (mismo patron que fadeOutForLevelStart)
    m_activeSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_currentLayer = DynSongLayer::None;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    float currentVol = getDynamicVolume();
    m_state = DynState::FadingOut;
    fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::RestoreMenu);
}

// ─── fadeOutForLevelStart ───────────────────────────────────────────
void DynamicSongManager::fadeOutForLevelStart() {
    if (!isActive()) return;

    cancelFade();

    if (isStreamingPreviewPending() && !m_streamingPreview) {
        stopStreamingPreview();
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    // Limpiar ownership inmediatamente (hooks dejan pasar musica de gameplay)
    m_activeSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_currentLayer = DynSongLayer::None;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    float currentVol = getDynamicVolume();

    if (currentVol <= 0.01f) {
        stopStreamingPreview();
        m_state = DynState::Idle;
        return;
    }

    m_state = DynState::FadingOut;
    fadeVolume(currentVol, 0.0f, getFadeDurationSec(), PostFadeAction::Cleanup);
}

// ─── forceKill (unico corte brusco, para gameplay y shutdown) ───────
void DynamicSongManager::forceKill() {
    cancelFade();
    stopStreamingPreview();

    if (m_fadeNode) {
        m_fadeNode->release();
        m_fadeNode = nullptr;
    }

    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_savedMenuPos = 0;
    m_savedDynamicPosMs = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}

// ─── Suspension/reanudacion para profile music ──────────────────────
void DynamicSongManager::suspendPlaybackForExternalAudio() {
    cancelFade();

    // Si estamos en streaming preview, detener el stream
    if (m_streamingPreview || isStreamingPreviewPending()) {
        stopStreamingPreview();
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        auto* bgCh = getMainBgChannel(engine);
        if (bgCh) {
            unsigned int posMs = 0;
            if (bgCh->getPosition(&posMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                m_savedDynamicPosMs = posMs;
            }
        }
        engine->m_backgroundMusicChannel->stop();
    }

    m_state = DynState::Suspended;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();
}

void DynamicSongManager::resumeSuspendedPlayback() {
    if (m_state != DynState::Suspended) return;

    log::info("[DynSong] resumeSuspendedPlayback: path='{}' layer={} posMs={}",
              m_activeSongPath, static_cast<int>(m_currentLayer), m_savedDynamicPosMs);

    // Verificar precondiciones
    if (GameManager::get()->getGameVariable("0122")) {
        log::info("[DynSong] resumeSuspendedPlayback: musica desactivada (0122), idle");
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) {
        log::info("[DynSong] resumeSuspendedPlayback: volumen 0 o no engine, idle");
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    if (m_activeSongPath.empty() || !isInValidLayer()) {
        log::info("[DynSong] resumeSuspendedPlayback: path vacio={} validLayer={}, fallback a menu",
                  m_activeSongPath.empty(), isInValidLayer());
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        // Restaurar menu como fallback
        loadMenuTrack(engine->m_musicVolume);
        return;
    }

    // Cargar cancion y restaurar posicion
    log::info("[DynSong] resumeSuspendedPlayback: restaurando '{}'", m_activeSongPath);
    playOnMainChannel(m_activeSongPath, 0.0f);

    // Pausar canal inmediatamente para evitar glitch audible al hacer seek
    auto* bgCh = getMainBgChannel(engine);
    if (bgCh) {
        bgCh->setPaused(true);
    }

    paimon::setDynamicSongInteropActive(true);
    AudioContextCoordinator::get().claimDynamicAudio();
    // Grace period: prevent verifyPlayback false positive right after resume
    m_lastFadeCompleteTime = std::chrono::steady_clock::now();

    if (bgCh) {
        FMOD::Sound* currentSound = nullptr;
        bgCh->getCurrentSound(&currentSound);
        unsigned int lengthMs = 0;
        if (currentSound) currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

        if (m_savedDynamicPosMs > 0 && m_savedDynamicPosMs < lengthMs) {
            bgCh->setPosition(m_savedDynamicPosMs, FMOD_TIMEUNIT_MS);
        } else {
            applyRandomSeek();
        }

        bgCh->setPaused(false);
    }
    m_savedDynamicPosMs = 0;

    float targetVol = engine->m_musicVolume;
    m_state = DynState::FadingIn;
    fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
}

// ─── Volumen del canal principal ────────────────────────────────────
float DynamicSongManager::getDynamicVolume() const {
    if (m_streamingPreview && m_previewChannel) {
        float vol = 0.0f;
        m_previewChannel->getVolume(&vol);
        return vol;
    }
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return 0.0f;
    float vol = 0.0f;
    engine->m_backgroundMusicChannel->getVolume(&vol);
    return vol;
}

void DynamicSongManager::setDynamicVolume(float vol) {
    if (m_streamingPreview && m_previewChannel) {
        m_previewChannel->setVolume(std::clamp(vol, 0.0f, 1.0f));
        return;
    }
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::clamp(vol, 0.0f, 1.0f));
    }
}

// ─── Verificacion de playback ───────────────────────────────────────
bool DynamicSongManager::verifyPlayback() {
    if (!isActive() || m_activeSongPath.empty()) return false;
    if (!isInValidLayer()) return false;

    if (isStreamingPreviewPending()) return true;

    // Streaming preview siempre pasa verificacion
    if (m_streamingPreview) return true;

    // Guard: no verificar durante suspension — el audio se pauso intencionalmente
    // para profile music u otro audio externo, no es un hijack
    if (m_state == DynState::Suspended) {
        return true;
    }

    // Guard: skip verification during fade transitions — FMOD channel
    // metadata is transiently inconsistent while crossfading
    if (m_state == DynState::FadingIn || m_state == DynState::FadingOut) {
        return true;
    }

    // Guard: grace period after fade completes — FMOD needs time to
    // propagate channel metadata after the new sound starts
    auto elapsed = std::chrono::steady_clock::now() - m_lastFadeCompleteTime;
    if (elapsed < std::chrono::milliseconds(500)) {
        return true;
    }

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return false;

    bool isPlaying = false;
    engine->m_backgroundMusicChannel->isPlaying(&isPlaying);
    if (!isPlaying) return false;

    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return false;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return false;

    char nameBuffer[512] = {};
    currentSound->getName(nameBuffer, sizeof(nameBuffer));
    std::string currentName(nameBuffer);
    if (currentName.empty()) return false;

    auto getFileName = [](const std::string& path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(pos + 1) : path;
    };

    return getFileName(m_activeSongPath) == getFileName(currentName);
}

void DynamicSongManager::onPlaybackHijacked() {
    cancelFade();
    stopStreamingPreview();
    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();
}

// ─── Streaming preview (canciones no descargadas) ───────────────────

// Nodo auxiliar que hace poll cada 2s para verificar si la cancion
// se descargo localmente y hacer swap.
class DynStreamPollNode : public CCNode {
public:
    static DynStreamPollNode* create() {
        auto* node = new DynStreamPollNode();
        if (node && node->init()) {
            return node; // no autorelease — se retiene manualmente
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void startPolling() {
        this->unschedule(schedule_selector(DynStreamPollNode::pollTick));
        this->schedule(schedule_selector(DynStreamPollNode::pollTick), 0.1f);
    }

    void stopPolling() {
        this->unschedule(schedule_selector(DynStreamPollNode::pollTick));
    }

private:
    void pollTick(float dt) {
        DynamicSongManager::get()->checkPreviewSwap();
    }
};

void DynamicSongManager::startStreamingPreview(GJGameLevel* level) {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song-stream-preview")) return;
    if (m_streamingPreview || isStreamingPreviewPending()) return;

    int songID = level->m_songID;
    if (songID <= 0) return;

    auto* mdm = MusicDownloadManager::sharedState();
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!mdm || !engine || !engine->m_system) return;
    if (engine->m_musicVolume <= 0.0f) return;

    m_previewSongID = songID;
    m_currentPlayingLevelID = level->m_levelID.value();
    m_activeSongPath = "[stream:" + std::to_string(songID) + "]";

    auto* songInfo = mdm->getSongInfoObject(songID);
    if (!songInfo || songInfo->m_songUrl.empty()) {
        mdm->getSongInfo(songID, true);
        m_previewAwaitingSongInfo = true;
        m_previewRequestStartTime = std::chrono::steady_clock::now();

        if (!m_streamPollNode) {
            m_streamPollNode = DynStreamPollNode::create();
            m_streamPollNode->retain();
        }
        static_cast<DynStreamPollNode*>(m_streamPollNode)->startPolling();

        log::info("[DynSong] startStreamingPreview: esperando song info para songID={}", songID);
        return;
    }

    std::string url = songInfo->m_songUrl;

    log::info("[DynSong] startStreamingPreview: songID={} url='{}'", songID, url);

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT result = engine->m_system->createSound(
        url.c_str(),
        FMOD_CREATESTREAM | FMOD_NONBLOCKING | FMOD_LOOP_NORMAL | FMOD_2D,
        nullptr,
        &sound
    );
    if (result != FMOD_OK || !sound) {
        log::warn("[DynSong] startStreamingPreview: createSound failed ({})", static_cast<int>(result));
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_previewSongID = 0;
        return;
    }

    m_previewStreamSound = sound;
    m_previewAwaitingSongInfo = false;
    m_streamingPreviewPending = true;
    m_previewRequestStartTime = std::chrono::steady_clock::now();

    if (!m_streamPollNode) {
        m_streamPollNode = DynStreamPollNode::create();
        m_streamPollNode->retain();
    }
    static_cast<DynStreamPollNode*>(m_streamPollNode)->startPolling();

    log::info("[DynSong] startStreamingPreview: apertura no bloqueante iniciada para songID={}", songID);
}

void DynamicSongManager::stopStreamingPreview() {
    if (!m_streamingPreview && !isStreamingPreviewPending()) return;

    log::info("[DynSong] stopStreamingPreview: limpiando songID={}", m_previewSongID);

    if (m_streamPollNode) {
        static_cast<DynStreamPollNode*>(m_streamPollNode)->stopPolling();
    }

    if (m_previewChannel) {
        m_previewChannel->stop();
        m_previewChannel = nullptr;
    }
    if (m_previewStreamSound) {
        m_previewStreamSound->release();
        m_previewStreamSound = nullptr;
    }

    m_previewAwaitingSongInfo = false;
    m_streamingPreviewPending = false;
    m_streamingPreview = false;
    m_previewSongID = 0;
    m_previewRequestStartTime = {};
}

void DynamicSongManager::checkPreviewSwap() {
    if (!m_streamingPreview && !isStreamingPreviewPending()) return;
    if (m_previewSongID <= 0) return;

    auto resetPreviewState = [this]() {
        stopStreamingPreview();
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
    };

    auto* mdm = MusicDownloadManager::sharedState();
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!mdm || !engine || !engine->m_system) {
        resetPreviewState();
        return;
    }

    if (m_previewAwaitingSongInfo) {
        auto* songInfo = mdm->getSongInfoObject(m_previewSongID);
        if (!songInfo || songInfo->m_songUrl.empty()) {
            auto elapsed = std::chrono::steady_clock::now() - m_previewRequestStartTime;
            if (elapsed > std::chrono::seconds(10)) {
                log::warn("[DynSong] checkPreviewSwap: timeout esperando song info para songID={}", m_previewSongID);
                resetPreviewState();
            }
            return;
        }

        FMOD::Sound* sound = nullptr;
        FMOD_RESULT result = engine->m_system->createSound(
            songInfo->m_songUrl.c_str(),
            FMOD_CREATESTREAM | FMOD_NONBLOCKING | FMOD_LOOP_NORMAL | FMOD_2D,
            nullptr,
            &sound
        );
        if (result != FMOD_OK || !sound) {
            log::warn("[DynSong] checkPreviewSwap: createSound failed tras song info ({})", static_cast<int>(result));
            resetPreviewState();
            return;
        }

        m_previewStreamSound = sound;
        m_previewAwaitingSongInfo = false;
        m_streamingPreviewPending = true;
        m_previewRequestStartTime = std::chrono::steady_clock::now();
        log::info("[DynSong] checkPreviewSwap: song info resuelta, apertura no bloqueante iniciada para songID={}", m_previewSongID);
        return;
    }

    if (m_streamingPreviewPending) {
        if (!m_previewStreamSound) {
            resetPreviewState();
            return;
        }

        FMOD_OPENSTATE openState = FMOD_OPENSTATE_READY;
        unsigned int percentBuffered = 0;
        bool starving = false;
        bool diskBusy = false;
        auto openResult = m_previewStreamSound->getOpenState(&openState, &percentBuffered, &starving, &diskBusy);
        if (openResult != FMOD_OK) {
            log::warn("[DynSong] checkPreviewSwap: getOpenState failed ({})", static_cast<int>(openResult));
            resetPreviewState();
            return;
        }

        if (openState == FMOD_OPENSTATE_ERROR) {
            log::warn("[DynSong] checkPreviewSwap: stream open state error para songID={}", m_previewSongID);
            resetPreviewState();
            return;
        }

        if (openState != FMOD_OPENSTATE_READY) {
            auto elapsed = std::chrono::steady_clock::now() - m_previewRequestStartTime;
            if (elapsed > std::chrono::seconds(15)) {
                log::warn("[DynSong] checkPreviewSwap: timeout abriendo stream para songID={}", m_previewSongID);
                resetPreviewState();
            }
            return;
        }

        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }

        auto* currentCh = getMainBgChannel(engine);
        if (currentCh) currentCh->stop();

        FMOD::Channel* ch = nullptr;
        auto playResult = engine->m_system->playSound(m_previewStreamSound, nullptr, true, &ch);
        if (playResult == FMOD_ERR_NOTREADY) {
            return;
        }
        if (playResult != FMOD_OK || !ch) {
            log::warn("[DynSong] checkPreviewSwap: playSound failed ({})", static_cast<int>(playResult));
            resetPreviewState();
            return;
        }

        ch->setVolume(0.0f);
        ch->setPriority(0);
        ch->setPaused(false);

        m_previewChannel = ch;
        m_streamingPreviewPending = false;
        m_streamingPreview = true;

        paimon::setDynamicSongInteropActive(true);
        AudioContextCoordinator::get().claimDynamicAudio();

        float targetVol = engine->m_musicVolume;
        m_state = DynState::FadingIn;
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);

        log::info("[DynSong] checkPreviewSwap: streaming iniciado para songID={} buffered={} starving={} diskBusy={}",
                  m_previewSongID, percentBuffered, starving, diskBusy);
        return;
    }

    if (!mdm->isSongDownloaded(m_previewSongID)) return;

    log::info("[DynSong] checkPreviewSwap: songID={} ahora descargado, swap a local", m_previewSongID);

    std::string localPath = mdm->pathForSong(m_previewSongID);
    if (localPath.empty()) return;

    // Detener stream
    stopStreamingPreview();

    // Reproducir archivo local con el flujo normal
    m_activeSongPath = localPath;
    playOnMainChannel(localPath, 0.0f);
    applyRandomSeek();

    float targetVol = engine->m_musicVolume;
    m_state = DynState::FadingIn;
    fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
}
