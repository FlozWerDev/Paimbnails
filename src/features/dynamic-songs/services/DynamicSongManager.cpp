#include "DynamicSongManager.hpp"
#include "DynamicSongConfig.hpp"
#include "DynamicSongSubmerge.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../framework/HookInterceptor.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <random>
#include <chrono>
#include <cmath>
#include <sstream>
#include <set>

using namespace geode::prelude;

namespace dynsong = paimon::dynsong;
using dynsong::SubmergeEffect;

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

class DynSongFadeNode : public cocos2d::CCNode {
public:
    static DynSongFadeNode* create() {
        auto* ret = new DynSongFadeNode();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void startFade(float fromVol, float toVol, float durationSec) {
        if (m_active) cancel();

        m_fromVol = fromVol;
        m_toVol = toVol;
        m_duration = std::max(durationSec, 0.016f);
        m_elapsed = 0.0f;
        m_active = true;

        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(DynSongFadeNode::onFadeTick),
            this, 0.0f, kCCRepeatForever, 0.0f, false
        );
    }

    void cancel() {
        if (!m_active) return;
        m_active = false;
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
    }

    bool isActive() const { return m_active; }

private:
    float m_fromVol = 0.f, m_toVol = 0.f;
    float m_duration = 0.f, m_elapsed = 0.f;
    bool m_active = false;

    void onFadeTick(float dt) {
        if (!m_active) return;
        if (paimon::isRuntimeShuttingDown()) {
            cancel();
            return;
        }

        m_elapsed += dt;
        float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);

        float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
        float vol = m_fromVol + (m_toVol - m_fromVol) * eT;

        DynamicSongManager::get()->setDynamicVolume(std::clamp(vol, 0.0f, 1.0f));

        if (t >= 1.0f) {
            m_active = false;
            auto* sched = cocos2d::CCDirector::get()->getScheduler();
            sched->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
            DynamicSongManager::get()->onFadeComplete();
        }
    }
};

DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

DynamicSongManager::~DynamicSongManager() {
    if (paimon::isRuntimeShuttingDown()) {
        m_fadeNode = nullptr;
        m_streamPollNode = nullptr;
        m_handoffWatchNode = nullptr;
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

    if (m_handoffWatchNode) {
        m_handoffWatchNode->unscheduleAllSelectors();
        m_handoffWatchNode->release();
        m_handoffWatchNode = nullptr;
    }
}

void DynamicSongManager::enterLayer(DynSongLayer layer) {
    m_currentLayer = layer;
}

void DynamicSongManager::exitLayer(DynSongLayer layer) {
    if (m_currentLayer == layer) {
        m_currentLayer = DynSongLayer::None;
    }
}

float DynamicSongManager::getFadeDurationSec() const {
    return dynsong::config().fadeSeconds;
}

// Target volume combines the game slider and feature trim.
float DynamicSongManager::dynamicTargetVolume() const {
    auto* engine = FMODAudioEngine::sharedEngine();
    float const base = engine ? engine->m_musicVolume : 1.0f;
    return std::clamp(base * (dynsong::config().volumePct / 100.f), 0.0f, 1.0f);
}

// Streaming previews use their own channel; local songs use the shared group.
FMOD::ChannelControl* DynamicSongManager::currentChannelControl() const {
    if (m_streamingPreview && m_previewChannel) {
        return static_cast<FMOD::ChannelControl*>(m_previewChannel);
    }
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return nullptr;
    return static_cast<FMOD::ChannelControl*>(engine->m_backgroundMusicChannel);
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
    float menuVol = engine ? engine->m_musicVolume : 1.0f;

    switch (m_postFadeAction) {
    case PostFadeAction::PlayPending: {
// Fade complete: load the pending song and fade in.
        stopStreamingPreview();
        playOnMainChannel(m_pendingSongPath, 0.0f);
        applyStartPosition(m_currentPlayingLevelID);
        m_activeSongPath = m_pendingSongPath;
        m_pendingSongPath.clear();
        m_state = DynState::FadingIn;
        m_lastFadeCompleteTime = std::chrono::steady_clock::now();
        fadeVolume(0.0f, dynamicTargetVolume(), getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::RestoreMenu: {
        stopStreamingPreview();
        SubmergeEffect::get().release();
        loadMenuTrack(0.0f);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        fadeVolume(0.0f, menuVol, getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::Cleanup:
        stopStreamingPreview();
        SubmergeEffect::get().release();
        m_state = DynState::Idle;
        break;

    case PostFadeAction::None:
        if (m_state == DynState::FadingIn) {
            m_state = DynState::Playing;
            m_lastFadeCompleteTime = std::chrono::steady_clock::now();
        }
        break;
    }

    m_postFadeAction = PostFadeAction::None;
}

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

void DynamicSongManager::applyRandomSeek(FMOD::Channel* existingCh) {
    auto engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = existingCh ? existingCh : getMainBgChannel(engine);
    if (!bgCh) return;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return;

    unsigned int lengthMs = 0;
    currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs <= 10000) return;

    auto const& cfg = dynsong::config();
    unsigned int minStart = static_cast<unsigned int>(lengthMs * (cfg.randomMinPct / 100.f));
    unsigned int maxStart = static_cast<unsigned int>(lengthMs * (cfg.randomMaxPct / 100.f));
    if (maxStart <= minStart) return;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
    bgCh->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
}

void DynamicSongManager::applyStartPosition(int levelID, FMOD::Channel* existingCh) {
    auto const& cfg = dynsong::config();

    switch (cfg.startMode) {
    case dynsong::StartMode::Beginning: {
        auto engine = FMODAudioEngine::sharedEngine();
        auto* bgCh = existingCh ? existingCh : getMainBgChannel(engine);
        if (bgCh) bgCh->setPosition(0, FMOD_TIMEUNIT_MS);
        return;
    }
    case dynsong::StartMode::Resume: {
        auto it = m_resumePositions.find(levelID);
        if (it == m_resumePositions.end() || it->second == 0) {
            applyRandomSeek(existingCh);
            return;
        }
        auto engine = FMODAudioEngine::sharedEngine();
        auto* bgCh = existingCh ? existingCh : getMainBgChannel(engine);
        if (!bgCh) return;

        FMOD::Sound* currentSound = nullptr;
        bgCh->getCurrentSound(&currentSound);
        unsigned int lengthMs = 0;
        if (currentSound) currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

// Clamp positions saved by older versions.
        if (lengthMs > 0 && it->second < lengthMs) {
            bgCh->setPosition(it->second, FMOD_TIMEUNIT_MS);
        } else {
            applyRandomSeek(existingCh);
        }
        return;
    }
    case dynsong::StartMode::Random:
    case dynsong::StartMode::Count:
        break;
    }
    applyRandomSeek(existingCh);
}

void DynamicSongManager::rememberPosition() {
    if (dynsong::config().startMode != dynsong::StartMode::Resume) return;
    if (m_currentPlayingLevelID == 0 || m_streamingPreview) return;

    auto* bgCh = getMainBgChannel(FMODAudioEngine::sharedEngine());
    if (!bgCh) return;

    unsigned int posMs = 0;
    if (bgCh->getPosition(&posMs, FMOD_TIMEUNIT_MS) != FMOD_OK || posMs == 0) return;

    if (m_resumePositions.size() >= MAX_RESUME_LEVELS
        && !m_resumePositions.count(m_currentPlayingLevelID)) {
        m_resumePositions.erase(m_resumePositions.begin());
    }
    m_resumePositions[m_currentPlayingLevelID] = posMs;
}

std::vector<std::string> DynamicSongManager::getAllSongPaths(GJGameLevel* level) {
    std::vector<std::string> paths;
    std::set<int> seenIds;
    auto mdm = MusicDownloadManager::sharedState();

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

    switch (dynsong::config().rotationMode) {
    case dynsong::RotationMode::First:
        return allPaths[0];
    case dynsong::RotationMode::Random: {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, allPaths.size() - 1);
        return allPaths[dist(gen)];
    }
    case dynsong::RotationMode::Rotate:
    case dynsong::RotationMode::Count:
        break;
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

void DynamicSongManager::playSong(GJGameLevel* level) {

    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    if (!level) return;
    if (!isInValidLayer()) return;
    if (m_currentLayer == DynSongLayer::LevelSelect && !dynsong::config().inLevelSelect) return;
    if (GameManager::get()->getGameVariable("0122")) return;
    if (paimon::isVideoAudioInteropActive()) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) return;
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - m_lastPlaySongTime;
        if (elapsed < std::chrono::milliseconds(200)) return;
        m_lastPlaySongTime = now;
    }
    {
        paimon::HookContext ctx;
        ctx.action = "dynamic-play";
        ctx.levelID = level->m_levelID.value();
        auto result = paimon::HookInterceptor::get().runPreHooks(ctx);
        if (!result.isAllowed()) return;
    }

    int levelId = level->m_levelID.value();
    float targetVol = dynamicTargetVolume();

    if (m_state == DynState::Handoff) {
        if (levelId == m_currentPlayingLevelID) {
            cancelGameplayHandoff();
            return;
        }
        finishGameplayHandoff();
    }

    if (levelId != m_currentPlayingLevelID && (m_streamingPreview || isStreamingPreviewPending() || m_awaitingDownloadOnly)) {
        cancelFade();
        stopStreamingPreview();
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
    }

    if (isActive() && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        if (m_state == DynState::Playing || m_state == DynState::FadingIn) {
            paimon::setDynamicSongInteropActive(true);
            return;
        }
    }

    std::string songPath;
    if (m_state != DynState::Idle && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        songPath = m_activeSongPath;
    } else {
        songPath = getNextRotationSong(level);
    }
    if (songPath.empty()) {
        // No local song; try streaming preview.
        startStreamingPreview(level);
        return;
    }

    if (m_state == DynState::Suspended) {
        m_state = DynState::Idle;
    }

    cancelFade();

    m_activeSongPath = songPath;
    m_currentPlayingLevelID = levelId;
    paimon::setDynamicSongInteropActive(true);

    if (m_state == DynState::Idle) {
        if (engine->isMusicPlaying(0) && !isActive()) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        playOnMainChannel(songPath, 0.0f);
        applyStartPosition(levelId);
        m_state = DynState::FadingIn;

        auto const& sub = dynsong::config().submerge;
        auto const sinceGameplay = std::chrono::steady_clock::now() - m_surfaceRequestTime;
        bool const surface = m_surfaceLevelID != 0 && m_surfaceLevelID == levelId
                          && sinceGameplay < std::chrono::seconds(15)
                          && sub.enabled && sub.onLevelExit;
        m_surfaceLevelID = 0;
        if (surface) {
            SubmergeEffect::get().bindTarget(currentChannelControl());
            SubmergeEffect::get().snapTo(1.0f);
            SubmergeEffect::get().rampTo(0.0f, sub.surfaceSeconds);
        } else {
            SubmergeEffect::get().release();
        }

        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
    } else {
        float currentVol = getDynamicVolume();
        m_pendingSongPath = songPath;
        m_state = DynState::FadingOut;
        fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::PlayPending);
    }

    AudioContextCoordinator::get().claimDynamicAudio();
}

void DynamicSongManager::stopSong() {
    if (!isActive()) return;

    cancelFade();
    stopHandoffWatch();
    rememberPosition();
    m_surfaceLevelID = 0;
    m_handoffLayer = DynSongLayer::None;
    m_handoffLevelID = 0;

// Download-watch mode has no local channel to fade; stop polling and go idle.
    if (m_awaitingDownloadOnly) {
        stopStreamingPreview();
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

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
        SubmergeEffect::get().release();
        loadMenuTrack(FMODAudioEngine::sharedEngine()->m_musicVolume);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    m_activeSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_currentLayer = DynSongLayer::None;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    float currentVol = getDynamicVolume();
    m_state = DynState::FadingOut;
    if (SubmergeEffect::get().isEngaged()) {
        SubmergeEffect::get().rampTo(0.0f, getFadeDurationSec());
    }
    fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::RestoreMenu);
}

void DynamicSongManager::fadeOutForLevelStart() {
    if (!isActive()) return;

    cancelFade();
    rememberPosition();

// No local channel in download-watch mode; stop polling.
    if (m_awaitingDownloadOnly) {
        stopStreamingPreview();
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

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

// Gameplay handoff: the muffled window between pressing play and the level.

namespace {

// Watch for the level to start, cancellation, or leaving the screen.
class DynHandoffWatchNode : public CCNode {
public:
    static DynHandoffWatchNode* create() {
        auto* node = new DynHandoffWatchNode();
        if (node && node->init()) return node;
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void start() {
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynHandoffWatchNode::onTick), this);
        scheduler->scheduleSelector(
            schedule_selector(DynHandoffWatchNode::onTick),
            this, 0.1f, kCCRepeatForever, 0.0f, false
        );
    }

    void stop() {
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynHandoffWatchNode::onTick), this);
    }

private:
    void onTick(float dt) {
        if (paimon::isRuntimeShuttingDown()) {
            stop();
            return;
        }
        DynamicSongManager::get()->handoffWatchTick(dt);
    }
};

bool levelScreenStillShowing() {
    auto* director = CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;
    return scene->getChildByType<LevelInfoLayer>(0) != nullptr
        || scene->getChildByType<LevelSelectLayer>(0) != nullptr;
}

// Detect modal popups and loading circles on the scene or one child level down.
bool sceneHasBlockingLayer() {
    auto* director = CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;

    auto isBlocker = [](CCNode* node) {
        if (!node->isVisible()) return false;
        return typeinfo_cast<FLAlertLayer*>(node) != nullptr
            || typeinfo_cast<LoadingCircle*>(node) != nullptr;
    };

    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (isBlocker(child)) return true;
        if (!child->getChildren()) continue;
        for (auto* grandChild : CCArrayExt<CCNode*>(child->getChildren())) {
            if (isBlocker(grandChild)) return true;
        }
    }
    return false;
}

}

void DynamicSongManager::startHandoffWatch() {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!m_handoffWatchNode) {
        m_handoffWatchNode = DynHandoffWatchNode::create();
        if (!m_handoffWatchNode) return;
        m_handoffWatchNode->retain();
    }
    static_cast<DynHandoffWatchNode*>(m_handoffWatchNode)->start();
}

void DynamicSongManager::stopHandoffWatch() {
    if (!m_handoffWatchNode) return;
    if (paimon::isRuntimeShuttingDown()) return;
    static_cast<DynHandoffWatchNode*>(m_handoffWatchNode)->stop();
}

void DynamicSongManager::handoffWatchTick(float dt) {
    if (paimon::isRuntimeShuttingDown() || m_state != DynState::Handoff) {
        stopHandoffWatch();
        return;
    }

    if (PlayLayer::get()) {
        finishGameplayHandoff();
        return;
    }

    m_handoffClock += dt;
    if (m_handoffClock < dynsong::config().submerge.holdSeconds) return;

    auto* director = CCDirector::get();
    if (!director || director->getNextScene() || director->isSendCleanupToScene()) return;

    if (!levelScreenStillShowing()) {
        finishGameplayHandoff();
        return;
    }

    if (!sceneHasBlockingLayer()) {
        cancelGameplayHandoff();
    }
}

void DynamicSongManager::submergeForLevelStart() {
    auto const& sub = dynsong::config().submerge;

    bool const audible = m_state == DynState::Playing
                      || m_state == DynState::FadingIn
                      || m_state == DynState::FadingOut;
    if (!sub.enabled || !isActive() || !audible) {
        fadeOutForLevelStart();
        return;
    }

    cancelFade();
    rememberPosition();

    m_handoffLayer = m_currentLayer;
    m_handoffLevelID = m_currentPlayingLevelID;
    m_handoffClock = 0.f;
    m_currentLayer = DynSongLayer::None;
    m_pendingSongPath.clear();
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    setDynamicVolume(dynamicTargetVolume());

    m_state = DynState::Handoff;

    SubmergeEffect::get().bindTarget(currentChannelControl());
    SubmergeEffect::get().rampTo(1.0f, sub.diveSeconds);
    startHandoffWatch();

    log::info("[DynSong] submergeForLevelStart: levelID={} dive={}s",
              m_handoffLevelID, sub.diveSeconds);
}

void DynamicSongManager::finishGameplayHandoff(int levelID) {
    stopHandoffWatch();

    int const surfaceID = levelID != 0 ? levelID : m_handoffLevelID;
    rememberPosition();

    resetToIdle(true);

    auto const& sub = dynsong::config().submerge;
    m_surfaceLevelID = (sub.enabled && sub.onLevelExit) ? surfaceID : 0;
    m_surfaceRequestTime = std::chrono::steady_clock::now();
}

void DynamicSongManager::cancelGameplayHandoff() {
    if (m_state != DynState::Handoff) return;
    stopHandoffWatch();

    auto const& cfg = dynsong::config();

    m_currentLayer = m_handoffLayer;
    m_handoffLayer = DynSongLayer::None;
    m_handoffLevelID = 0;
    m_surfaceLevelID = 0;
    m_state = m_activeSongPath.empty() ? DynState::Idle : DynState::Playing;

    AudioContextCoordinator::get().abortGameplayTransition(m_currentLayer);

    if (m_state == DynState::Idle) {
        SubmergeEffect::get().release();
        return;
    }

    paimon::setDynamicSongInteropActive(true);
    AudioContextCoordinator::get().claimDynamicAudio();
    m_lastFadeCompleteTime = std::chrono::steady_clock::now();

    setDynamicVolume(dynamicTargetVolume());
    SubmergeEffect::get().bindTarget(currentChannelControl());
    SubmergeEffect::get().rampTo(0.0f, cfg.submerge.surfaceSeconds);

    log::info("[DynSong] cancelGameplayHandoff: surfacing levelID={}", m_currentPlayingLevelID);
}

void DynamicSongManager::forceKill() {
    resetToIdle(false);
    m_surfaceLevelID = 0;
}

void DynamicSongManager::resetToIdle(bool stopOwnSound) {
    cancelFade();
    stopStreamingPreview();
    stopHandoffWatch();
    SubmergeEffect::get().release();

    if (m_fadeNode) {
        m_fadeNode->cancel();
        m_fadeNode->release();
        m_fadeNode = nullptr;
    }

    bool const ourSound = stopOwnSound && isOurSoundPlaying();

    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_handoffLayer = DynSongLayer::None;
    m_handoffLevelID = 0;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_savedMenuPos = 0;
    m_savedDynamicPosMs = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        if (ourSound) engine->m_backgroundMusicChannel->stop();
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}

bool DynamicSongManager::isOurSoundPlaying() const {
    if (m_activeSongPath.empty()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return false;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return false;

    char nameBuffer[512] = {};
    currentSound->getName(nameBuffer, sizeof(nameBuffer));
    std::string currentName(nameBuffer);
    if (currentName.empty()) return false;

    auto fileName = [](std::string const& path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(pos + 1) : path;
    };
    return fileName(m_activeSongPath) == fileName(currentName);
}

void DynamicSongManager::suspendPlaybackForExternalAudio() {
    cancelFade();
    stopHandoffWatch();
    rememberPosition();
    SubmergeEffect::get().release();

    if (m_streamingPreview || isStreamingPreviewPending() || m_awaitingDownloadOnly) {
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

    if (GameManager::get()->getGameVariable("0122")) {
        log::info("[DynSong] resumeSuspendedPlayback: music disabled (0122), idle");
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) {
        log::info("[DynSong] resumeSuspendedPlayback: volume 0 or no engine, idle");
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    if (m_activeSongPath.empty() || !isInValidLayer()) {
        log::info("[DynSong] resumeSuspendedPlayback: path empty={} validLayer={}, fallback to menu",
                  m_activeSongPath.empty(), isInValidLayer());
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        loadMenuTrack(engine->m_musicVolume);
        return;
    }

    log::info("[DynSong] resumeSuspendedPlayback: restaurando '{}'", m_activeSongPath);
    playOnMainChannel(m_activeSongPath, 0.0f);

    auto* bgCh = getMainBgChannel(engine);
    if (bgCh) {
        bgCh->setPaused(true);
    }

    paimon::setDynamicSongInteropActive(true);
    AudioContextCoordinator::get().claimDynamicAudio();
    m_lastFadeCompleteTime = std::chrono::steady_clock::now();

    if (bgCh) {
        FMOD::Sound* currentSound = nullptr;
        bgCh->getCurrentSound(&currentSound);
        unsigned int lengthMs = 0;
        if (currentSound) currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

        if (m_savedDynamicPosMs > 0 && m_savedDynamicPosMs < lengthMs) {
            bgCh->setPosition(m_savedDynamicPosMs, FMOD_TIMEUNIT_MS);
        } else {
            applyStartPosition(m_currentPlayingLevelID, bgCh);
        }

        bgCh->setPaused(false);
    }
    m_savedDynamicPosMs = 0;

    m_state = DynState::FadingIn;
    fadeVolume(0.0f, dynamicTargetVolume(), getFadeDurationSec(), PostFadeAction::None);
}

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

bool DynamicSongManager::verifyPlayback() {
    if (!isActive() || m_activeSongPath.empty()) return false;
    if (!isInValidLayer()) return false;

    if (isStreamingPreviewPending()) return true;

    if (m_streamingPreview) return true;

    // Suspension is intentional external audio, not a hijack.
    if (m_state == DynState::Suspended) {
        return true;
    }

    // Gameplay handoff temporarily gives ownership up while audio remains audible.
    if (m_state == DynState::Handoff) {
        return true;
    }

    // FMOD metadata is transient during crossfades.
    if (m_state == DynState::FadingIn || m_state == DynState::FadingOut) {
        return true;
    }

    // Allow FMOD time to propagate metadata after a fade.
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
    stopHandoffWatch();
    SubmergeEffect::get().release();
    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_handoffLayer = DynSongLayer::None;
    m_handoffLevelID = 0;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();
}

// Streaming preview.

// Poll for the local download and swap to it when ready.
class DynStreamPollNode : public CCNode {
public:
    static DynStreamPollNode* create() {
        auto* node = new DynStreamPollNode();
        if (node && node->init()) {
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void startPolling() {
        // The detached node would be paused by CCNode::schedule(); register it
        // directly with paused=false.
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynStreamPollNode::pollTick), this);
        scheduler->scheduleSelector(
            schedule_selector(DynStreamPollNode::pollTick),
            this, 0.1f, kCCRepeatForever, 0.0f, false
        );
    }

    void stopPolling() {
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynStreamPollNode::pollTick), this);
    }

private:
    void pollTick(float /*dt*/) {
        if (paimon::isRuntimeShuttingDown()) {
            stopPolling();
            return;
        }
        DynamicSongManager::get()->checkPreviewSwap();
    }
};

void DynamicSongManager::startStreamingPreview(GJGameLevel* level) {
    if (m_streamingPreview || isStreamingPreviewPending() || m_awaitingDownloadOnly) return;

    int songID = level->m_songID;
    if (songID <= 0) return;

    auto* mdm = MusicDownloadManager::sharedState();
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!mdm || !engine || !engine->m_system) return;
    if (engine->m_musicVolume <= 0.0f) return;

    // Watch for the local download even when streaming is disabled.
    if (!dynsong::config().streamPreview) {
        m_previewSongID = songID;
        m_currentPlayingLevelID = level->m_levelID.value();
        m_awaitingDownloadOnly = true;

        if (!m_streamPollNode) {
            m_streamPollNode = DynStreamPollNode::create();
            m_streamPollNode->retain();
        }
        static_cast<DynStreamPollNode*>(m_streamPollNode)->startPolling();

        log::info("[DynSong] startStreamingPreview: preview disabled, awaiting download for songID={}", songID);
        return;
    }

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

        log::info("[DynSong] startStreamingPreview: waiting for song info for songID={}", songID);
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

    log::info("[DynSong] startStreamingPreview: non-blocking open started for songID={}", songID);
}

void DynamicSongManager::stopStreamingPreview() {
    if (!m_streamingPreview && !isStreamingPreviewPending() && !m_awaitingDownloadOnly) return;

    log::info("[DynSong] stopStreamingPreview: cleaning up songID={}", m_previewSongID);

    if (m_streamPollNode) {
        static_cast<DynStreamPollNode*>(m_streamPollNode)->stopPolling();
    }

    if (m_previewChannel) {
        if (SubmergeEffect::get().isEngaged()) SubmergeEffect::get().release();
        SubmergeEffect::get().bindTarget(nullptr);
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
    m_awaitingDownloadOnly = false;
    m_previewSongID = 0;
    m_previewRequestStartTime = {};
    m_previewPlayAttemptSince = {};
}

void DynamicSongManager::checkPreviewSwap() {
    if (paimon::isRuntimeShuttingDown()) {
        stopStreamingPreview();
        return;
    }
    if (!m_streamingPreview && !isStreamingPreviewPending() && !m_awaitingDownloadOnly) return;
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

    if (m_awaitingDownloadOnly) {
        if (!isInValidLayer()) {
            resetPreviewState();
            return;
        }
        if (!mdm->isSongDownloaded(m_previewSongID)) return;

        std::string localPath = mdm->pathForSong(m_previewSongID);
        if (localPath.empty()) return;

        log::info("[DynSong] checkPreviewSwap: songID={} downloaded, starting playback", m_previewSongID);

        int levelID = m_currentPlayingLevelID;
        stopStreamingPreview();

        m_activeSongPath = localPath;
        m_currentPlayingLevelID = levelID;

        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }

        playOnMainChannel(localPath, 0.0f);
        applyStartPosition(levelID);

        paimon::setDynamicSongInteropActive(true);

        m_state = DynState::FadingIn;
        fadeVolume(0.0f, dynamicTargetVolume(), getFadeDurationSec(), PostFadeAction::None);
        return;
    }

    if (m_previewAwaitingSongInfo) {
        auto* songInfo = mdm->getSongInfoObject(m_previewSongID);
        if (!songInfo || songInfo->m_songUrl.empty()) {
            auto elapsed = std::chrono::steady_clock::now() - m_previewRequestStartTime;
            if (elapsed > std::chrono::seconds(10)) {
                log::warn("[DynSong] checkPreviewSwap: timeout waiting for song info for songID={}", m_previewSongID);
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
            log::warn("[DynSong] checkPreviewSwap: createSound failed after song info ({})", static_cast<int>(result));
            resetPreviewState();
            return;
        }

        m_previewStreamSound = sound;
        m_previewAwaitingSongInfo = false;
        m_streamingPreviewPending = true;
        m_previewRequestStartTime = std::chrono::steady_clock::now();
        log::info("[DynSong] checkPreviewSwap: song info resolved, non-blocking open started for songID={}", m_previewSongID);
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
            m_previewPlayAttemptSince = {};
            auto elapsed = std::chrono::steady_clock::now() - m_previewRequestStartTime;
            if (elapsed > std::chrono::seconds(15)) {
                log::warn("[DynSong] checkPreviewSwap: timeout opening stream for songID={}", m_previewSongID);
                resetPreviewState();
            }
            return;
        }

        if (m_previewPlayAttemptSince.time_since_epoch().count() == 0) {
            m_previewPlayAttemptSince = std::chrono::steady_clock::now();
        }

        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }

        auto* currentCh = getMainBgChannel(engine);
        if (currentCh) currentCh->stop();

        FMOD::Channel* ch = nullptr;
        auto playResult = engine->m_system->playSound(m_previewStreamSound, nullptr, true, &ch);
        if (playResult == FMOD_ERR_NOTREADY) {
            auto elapsed = std::chrono::steady_clock::now() - m_previewPlayAttemptSince;
            if (elapsed > std::chrono::seconds(10)) {
                log::warn("[DynSong] checkPreviewSwap: timeout FMOD_ERR_NOTREADY for songID={}", m_previewSongID);
                resetPreviewState();
            }
            return;
        }
        m_previewPlayAttemptSince = {};
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

        m_state = DynState::FadingIn;
        fadeVolume(0.0f, dynamicTargetVolume(), getFadeDurationSec(), PostFadeAction::None);

        log::info("[DynSong] checkPreviewSwap: streaming started for songID={} buffered={} starving={} diskBusy={}",
                  m_previewSongID, percentBuffered, starving, diskBusy);
        return;
    }

    if (!mdm->isSongDownloaded(m_previewSongID)) return;

    log::info("[DynSong] checkPreviewSwap: songID={} now downloaded, swapping to local", m_previewSongID);

    std::string localPath = mdm->pathForSong(m_previewSongID);
    if (localPath.empty()) return;

    stopStreamingPreview();

    m_activeSongPath = localPath;
    playOnMainChannel(localPath, 0.0f);
    applyStartPosition(m_currentPlayingLevelID);

    m_state = DynState::FadingIn;
    fadeVolume(0.0f, dynamicTargetVolume(), getFadeDurationSec(), PostFadeAction::None);
}
