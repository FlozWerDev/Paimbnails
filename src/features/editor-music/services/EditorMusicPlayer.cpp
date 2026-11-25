#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "EditorMusicPlayer.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../menu-music/services/MenuMusicCopy.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"
#include "../../menu-music/services/SongCoverCache.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;
using paimon::menumusic::MusicTrack;
using paimon::menumusic::SongCoverCache;
using paimon::menumusic::TrackSource;

namespace paimon::editormusic {

namespace {

constexpr char const* kVolumeKey = "editor-music-volume";
constexpr char const* kMutedKey = "editor-music-muted";
constexpr char const* kShuffleKey = "editor-music-shuffle";
constexpr char const* kRepeatKey = "editor-music-repeat";
constexpr char const* kLastKey = "editor-music-last";
constexpr char const* kSourceKey = "editor-music-source";
constexpr char const* kPlaylistKey = "editor-music-playlist";
constexpr char const* kPlaytestKey = "editor-music-playtest-mode";
constexpr char const* kAutoPlayKey = "editor-music-autoplay";
constexpr char const* kToastKey = "editor-music-toast";
constexpr char const* kAnimKey = "editor-music-animations";
constexpr char const* kSkinKey = "editor-music-skin";

constexpr char const* kFxEnabledKey = "editor-music-fx-enabled";
constexpr char const* kFxPresetKey = "editor-music-fx-preset";
constexpr char const* kFxSpeedKey = "editor-music-fx-speed";
constexpr char const* kFxBassKey = "editor-music-fx-bass";
constexpr char const* kFxTrebleKey = "editor-music-fx-treble";
constexpr char const* kFxLowpassKey = "editor-music-fx-lowpass";
constexpr char const* kFxReverbKey = "editor-music-fx-reverb";
constexpr char const* kFxEchoKey = "editor-music-fx-echo";

constexpr std::size_t kMaxHistory = 32;
constexpr float kDuckFade = 0.16f;

bool playable(MusicTrack const& track) {
    if (track.audioPath.empty() || track.blacklisted) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(paimon::assets::pathFromUtf8(track.audioPath), ec) && !ec;
}

std::size_t randomIndex(std::size_t count) {
    static std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng);
}

float wetLevelDb(float percent) {
    if (percent <= 0.01f) return -80.f;
    return std::clamp(20.f * std::log10(percent / 100.f), -80.f, 0.f);
}

void normalizeFx(EditorFxConfig& cfg) {
    cfg.speed = std::clamp(cfg.speed, 0.5f, 1.5f);
    cfg.bassDb = std::clamp(cfg.bassDb, -12.f, 10.f);
    cfg.trebleDb = std::clamp(cfg.trebleDb, -12.f, 10.f);
    cfg.lowpassHz = std::clamp(cfg.lowpassHz, 500.f, 22000.f);
    cfg.reverbMix = std::clamp(cfg.reverbMix, 0.f, 100.f);
    cfg.echoMix = std::clamp(cfg.echoMix, 0.f, 100.f);
}

} // namespace

char const* fxPresetName(EditorFxPreset preset) {
    switch (preset) {
        case EditorFxPreset::Original:    return "Original";
        case EditorFxPreset::SlowReverb:  return "Slow + Rev";
        case EditorFxPreset::Nightcore:   return "Nightcore";
        case EditorFxPreset::BassBoost:   return "Bass Boost";
        case EditorFxPreset::Lofi:        return "Lo-Fi";
        case EditorFxPreset::Underwater:  return "Underwater";
        case EditorFxPreset::ConcertHall: return "Hall";
        case EditorFxPreset::Custom:      return "Custom";
        case EditorFxPreset::Count:       break;
    }
    return "Original";
}

EditorFxConfig fxPresetConfig(EditorFxPreset preset) {
    EditorFxConfig cfg;
    cfg.preset = preset;
    cfg.enabled = preset != EditorFxPreset::Original;

    switch (preset) {
        case EditorFxPreset::SlowReverb:
            cfg.speed = 0.80f;
            cfg.bassDb = 2.f;
            cfg.lowpassHz = 15500.f;
            cfg.reverbMix = 45.f;
            break;
        case EditorFxPreset::Nightcore:
            cfg.speed = 1.22f;
            cfg.bassDb = -1.f;
            cfg.trebleDb = 3.f;
            cfg.reverbMix = 8.f;
            break;
        case EditorFxPreset::BassBoost:
            cfg.bassDb = 7.f;
            break;
        case EditorFxPreset::Lofi:
            cfg.speed = 0.90f;
            cfg.bassDb = 2.5f;
            cfg.trebleDb = -5.f;
            cfg.lowpassHz = 7200.f;
            cfg.reverbMix = 16.f;
            break;
        case EditorFxPreset::Underwater:
            cfg.speed = 0.88f;
            cfg.bassDb = 4.f;
            cfg.lowpassHz = 1150.f;
            cfg.reverbMix = 20.f;
            break;
        case EditorFxPreset::ConcertHall:
            cfg.reverbMix = 62.f;
            cfg.echoMix = 12.f;
            break;
        case EditorFxPreset::Custom:
            cfg.enabled = true;
            break;
        case EditorFxPreset::Original:
        case EditorFxPreset::Count:
            break;
    }
    return cfg;
}

char const* queueSourceName(EditorQueueSource source) {
    switch (source) {
        case EditorQueueSource::All:          return "Todas";
        case EditorQueueSource::Favorites:    return "Favoritas";
        case EditorQueueSource::Playlist:     return "Playlist";
        case EditorQueueSource::GeometryDash: return "GD Songs";
        case EditorQueueSource::Local:        return "Locales";
        case EditorQueueSource::Count:        break;
    }
    return "Todas";
}

char const* playtestBehaviorName(PlaytestBehavior behavior) {
    switch (behavior) {
        case PlaytestBehavior::PauseAndResume: return "Pausar y reanudar";
        case PlaytestBehavior::DuckVolume:     return "Bajar volumen";
        case PlaytestBehavior::KeepPlaying:    return "Seguir sonando";
        case PlaytestBehavior::Count:          break;
    }
    return "Pausar y reanudar";
}

EditorMusicPlayer& EditorMusicPlayer::get() {
    static EditorMusicPlayer instance;
    return instance;
}

EditorMusicPlayer::EditorMusicPlayer() {
    auto* mod = Mod::get();
    m_volume = std::clamp(static_cast<float>(mod->getSavedValue<double>(kVolumeKey, 0.6)), 0.f, 1.f);
    m_muted = mod->getSavedValue<bool>(kMutedKey, false);
    m_shuffle = mod->getSavedValue<bool>(kShuffleKey, false);

    auto repeat = mod->getSavedValue<int64_t>(kRepeatKey, 1);
    m_repeat = repeat == 0 ? RepeatMode::Off : (repeat == 2 ? RepeatMode::One : RepeatMode::All);

    auto srcRaw = static_cast<int>(mod->getSavedValue<int64_t>(kSourceKey, 0));
    if (srcRaw >= 0 && srcRaw < static_cast<int>(EditorQueueSource::Count)) {
        m_source = static_cast<EditorQueueSource>(srcRaw);
    }
    m_playlistId = mod->getSavedValue<std::string>(kPlaylistKey, "");

    auto ptRaw = static_cast<int>(mod->getSavedValue<int64_t>(kPlaytestKey, 0));
    if (ptRaw >= 0 && ptRaw < static_cast<int>(PlaytestBehavior::Count)) {
        m_playtestBehavior = static_cast<PlaytestBehavior>(ptRaw);
    }

    m_autoPlayOnEnter = mod->getSavedValue<bool>(kAutoPlayKey, false);
    m_showToastOnChange = mod->getSavedValue<bool>(kToastKey, true);
    m_animationsEnabled = mod->getSavedValue<bool>(kAnimKey, true);
    m_panelSkin = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>(kSkinKey, 0)), 0, 3);

    m_fx.enabled = mod->getSavedValue<bool>(kFxEnabledKey, false);
    auto presetRaw = static_cast<int>(mod->getSavedValue<int64_t>(kFxPresetKey, 0));
    if (presetRaw >= 0 && presetRaw < static_cast<int>(EditorFxPreset::Count)) {
        m_fx.preset = static_cast<EditorFxPreset>(presetRaw);
    }
    m_fx.speed = static_cast<float>(mod->getSavedValue<double>(kFxSpeedKey, 1.0));
    m_fx.bassDb = static_cast<float>(mod->getSavedValue<double>(kFxBassKey, 0.0));
    m_fx.trebleDb = static_cast<float>(mod->getSavedValue<double>(kFxTrebleKey, 0.0));
    m_fx.lowpassHz = static_cast<float>(mod->getSavedValue<double>(kFxLowpassKey, 22000.0));
    m_fx.reverbMix = static_cast<float>(mod->getSavedValue<double>(kFxReverbKey, 0.0));
    m_fx.echoMix = static_cast<float>(mod->getSavedValue<double>(kFxEchoKey, 0.0));
    normalizeFx(m_fx);
}

void EditorMusicPlayer::ensureLibrarySyncStarted() {
    auto& lib = MenuMusicLibrary::get();
    lib.load();
    if (!m_libListenerToken) {
        m_libListenerToken = lib.addListener([this]() {
            if (!m_shuttingDown && !paimon::isRuntimeShuttingDown()) {
                this->refreshQueue();
            }
        });
    }
    if (!m_syncRequested) {
        m_syncRequested = true;
        lib.syncDownloadedSongsAsync();
    }
}

bool EditorMusicPlayer::ensureQueue() {
    if (m_queue.empty()) refreshQueue();
    return !m_queue.empty();
}

void EditorMusicPlayer::refreshQueue() {
    ensureLibrarySyncStarted();
    auto& lib = MenuMusicLibrary::get();

    m_queue.clear();
    if (m_source == EditorQueueSource::Playlist) {
        if (auto* pl = lib.findPlaylist(m_playlistId)) {
            for (auto const& tid : pl->trackIds) {
                auto const* track = lib.findTrack(tid);
                if (track && playable(*track)) m_queue.push_back(tid);
            }
        }
    }

    if (m_queue.empty()) {
        // If Playlist was chosen but empty/missing, fall back smoothly or filter by source.
        for (auto const& track : lib.tracks()) {
            if (!playable(track)) continue;
            bool include = true;
            switch (m_source) {
                case EditorQueueSource::Favorites:
                    include = track.favorite;
                    break;
                case EditorQueueSource::GeometryDash:
                    include = (track.source == TrackSource::GeometryDash);
                    break;
                case EditorQueueSource::Local:
                    include = (track.source == TrackSource::Local ||
                               track.source == TrackSource::Downloaded);
                    break;
                case EditorQueueSource::Playlist:
                    // Empty playlist keeps queue empty unless user changes source.
                    include = false;
                    break;
                case EditorQueueSource::All:
                case EditorQueueSource::Count:
                    include = true;
                    break;
            }
            if (include) m_queue.push_back(track.id);
        }
    }

    if (m_trackId.empty()) {
        auto last = Mod::get()->getSavedValue<std::string>(kLastKey, "");
        if (std::find(m_queue.begin(), m_queue.end(), last) != m_queue.end()) m_trackId = last;
    }

    auto found = std::find(m_queue.begin(), m_queue.end(), m_trackId);
    m_index = found != m_queue.end() ? static_cast<std::size_t>(found - m_queue.begin()) : 0;
    notifyChanged();
}

std::vector<std::string> EditorMusicPlayer::upcomingTrackIds(std::size_t maxCount) const {
    std::vector<std::string> out;
    if (m_queue.empty() || maxCount == 0) return out;
    if (!m_queuedNextId.empty()) {
        out.push_back(m_queuedNextId);
    }
    std::size_t const count = std::min(maxCount, m_queue.size());
    for (std::size_t step = 1; step <= count && out.size() < maxCount; ++step) {
        std::size_t idx = (m_index + step) % m_queue.size();
        if (m_queue[idx] == m_trackId && m_queue.size() > 1) continue;
        if (m_queue[idx] == m_queuedNextId) continue;
        out.push_back(m_queue[idx]);
    }
    return out;
}

void EditorMusicPlayer::setSource(EditorQueueSource source, std::string const& playlistId) {
    m_source = source;
    if (source == EditorQueueSource::Playlist) {
        m_playlistId = playlistId;
    }
    Mod::get()->setSavedValue<int64_t>(kSourceKey, static_cast<int64_t>(m_source));
    Mod::get()->setSavedValue<std::string>(kPlaylistKey, m_playlistId);
    refreshQueue();
}

void EditorMusicPlayer::enqueueNext(std::string const& trackId) {
    auto const* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track || !playable(*track)) return;
    m_queuedNextId = trackId;
    notifyChanged();
}

std::string EditorMusicPlayer::trackName() const {
    if (m_trackId.empty()) return "";
    auto const* track = MenuMusicLibrary::get().findTrack(m_trackId);
    if (!track) return "";
    if (!track->displayName.empty()) return track->displayName;
    return paimon::paths::toUtf8(paimon::assets::pathFromUtf8(track->audioPath).stem());
}

std::string EditorMusicPlayer::trackArtist() const {
    if (m_trackId.empty()) return "";
    auto const* track = MenuMusicLibrary::get().findTrack(m_trackId);
    if (!track) return "";
    if (!track->artist.empty()) return track->artist;
    switch (track->source) {
        case TrackSource::GeometryDash: return "Newgrounds / GD";
        case TrackSource::Downloaded:   return "Descargada";
        case TrackSource::Local:        return "Archivo local";
        default:                        return "";
    }
}

std::string EditorMusicPlayer::trackCoverPath() const {
    if (m_trackId.empty()) return "";
    auto const* track = MenuMusicLibrary::get().findTrack(m_trackId);
    if (!track) return "";
    if (!track->coverPath.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(paimon::assets::pathFromUtf8(track->coverPath), ec) && !ec) {
            return track->coverPath;
        }
    }
    if (auto songId = paimon::menumusic::resolveGDSongId(track->audioPath, track->sourceUrl)) {
        auto cached = SongCoverCache::get().peekCachedCoverPaths(*songId);
        if (!cached.empty()) return cached.front();
    }
    return "";
}

int EditorMusicPlayer::trackSongId() const {
    if (m_trackId.empty()) return 0;
    auto const* track = MenuMusicLibrary::get().findTrack(m_trackId);
    if (!track) return 0;
    return paimon::menumusic::resolveGDSongId(track->audioPath, track->sourceUrl).value_or(0);
}

bool EditorMusicPlayer::ensureChannelGroup() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return false;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;
    if (m_group) return true;

    if (engine->m_system->createChannelGroup("PaimonEditorMusic", &m_group) != FMOD_OK || !m_group) {
        m_group = nullptr;
        return false;
    }
    return true;
}

bool EditorMusicPlayer::ensureDsps() {
    if (!ensureChannelGroup()) return false;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    if (m_eqDsp && m_lowpassDsp && m_reverbDsp && m_echoDsp && m_meterDsp) {
        return true;
    }

    auto create = [&](FMOD_DSP_TYPE type, FMOD::DSP*& out) {
        return out || engine->m_system->createDSPByType(type, &out) == FMOD_OK;
    };
    if (!create(FMOD_DSP_TYPE_THREE_EQ, m_eqDsp)
        || !create(FMOD_DSP_TYPE_LOWPASS, m_lowpassDsp)
        || !create(FMOD_DSP_TYPE_SFXREVERB, m_reverbDsp)
        || !create(FMOD_DSP_TYPE_ECHO, m_echoDsp)
        || !create(FMOD_DSP_TYPE_FADER, m_meterDsp)) {
        detachDsps();
        return false;
    }

    FMOD::DSP* chain[] = { m_eqDsp, m_lowpassDsp, m_reverbDsp, m_echoDsp };
    for (auto* dsp : chain) {
        if (m_group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp) != FMOD_OK) {
            detachDsps();
            return false;
        }
        dsp->setActive(true);
    }
    if (m_group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_meterDsp) != FMOD_OK) {
        detachDsps();
        return false;
    }
    m_meterDsp->setActive(true);
    m_meterDsp->setBypass(false);
    m_meterDsp->setParameterFloat(FMOD_DSP_FADER_GAIN, 0.f);
    m_meterDsp->setMeteringEnabled(false, true);
    return true;
}

void EditorMusicPlayer::applyFxToGroup() {
    if (!m_group) return;
    if (!ensureDsps()) return;

    bool const fxOn = m_fx.enabled && !m_fxBypassed;
    m_group->setPitch(fxOn ? m_fx.speed : 1.f);

    if (m_eqDsp) {
        m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_LOWGAIN, fxOn ? m_fx.bassDb : 0.f);
        m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_MIDGAIN, 0.f);
        m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_HIGHGAIN, fxOn ? m_fx.trebleDb : 0.f);
        m_eqDsp->setBypass(!fxOn || (std::abs(m_fx.bassDb) < 0.01f && std::abs(m_fx.trebleDb) < 0.01f));
    }
    if (m_lowpassDsp) {
        m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, fxOn ? m_fx.lowpassHz : 22000.f);
        m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.f);
        m_lowpassDsp->setBypass(!fxOn || m_fx.lowpassHz >= 21990.f);
    }
    if (m_reverbDsp) {
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 3800.f);
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DIFFUSION, 75.f);
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DENSITY, 75.f);
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_HIGHCUT, 16000.f);
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, 0.f);
        m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, wetLevelDb(m_fx.reverbMix));
        m_reverbDsp->setBypass(!fxOn || m_fx.reverbMix <= 0.01f);
    }
    if (m_echoDsp) {
        m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_DELAY, 340.f);
        m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_FEEDBACK, 28.f);
        m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_DRYLEVEL, 0.f);
        m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_WETLEVEL, wetLevelDb(m_fx.echoMix));
        m_echoDsp->setBypass(!fxOn || m_fx.echoMix <= 0.01f);
    }
}

void EditorMusicPlayer::detachDsps() {
    if (m_group) {
        m_group->setPitch(1.f);
        if (m_eqDsp) m_group->removeDSP(m_eqDsp);
        if (m_lowpassDsp) m_group->removeDSP(m_lowpassDsp);
        if (m_reverbDsp) m_group->removeDSP(m_reverbDsp);
        if (m_echoDsp) m_group->removeDSP(m_echoDsp);
        if (m_meterDsp) m_group->removeDSP(m_meterDsp);
    }

    auto release = [](FMOD::DSP*& dsp) {
        if (!dsp) return;
        dsp->release();
        dsp = nullptr;
    };
    release(m_eqDsp);
    release(m_lowpassDsp);
    release(m_reverbDsp);
    release(m_echoDsp);
    release(m_meterDsp);
}

bool EditorMusicPlayer::openFile(std::string const& path) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return false;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;
    if (!ensureChannelGroup()) return false;
    ensureDsps();

    releaseSound();

    auto result = engine->m_system->createSound(
        path.c_str(), FMOD_CREATESTREAM | FMOD_2D | FMOD_LOOP_OFF, nullptr, &m_sound);
    if (result != FMOD_OK || !m_sound) {
        log::warn("[EditorMusic] createSound failed ({}) for '{}'", static_cast<int>(result), path);
        m_sound = nullptr;
        return false;
    }

    result = engine->m_system->playSound(m_sound, m_group, true, &m_channel);
    if (result != FMOD_OK || !m_channel) {
        log::warn("[EditorMusic] playSound failed ({})", static_cast<int>(result));
        releaseSound();
        return false;
    }

    applyLoopMode();
    applyFxToGroup();
    m_fadeLevel = 0.05f;
    m_fadeTarget = 1.f;
    m_pendingPauseAfterFade = false;
    applyVolume();
    m_channel->setPaused(false);
    return true;
}

void EditorMusicPlayer::releaseSound() {
    if (m_channel) {
        m_channel->stop();
        m_channel = nullptr;
    }
    if (m_sound) {
        m_sound->release();
        m_sound = nullptr;
    }
}

bool EditorMusicPlayer::channelAlive() const {
    if (!m_channel) return false;
    bool playing = false;
    return m_channel->isPlaying(&playing) == FMOD_OK && playing;
}

void EditorMusicPlayer::applyLoopMode() {
    if (!m_channel) return;
    if (m_repeat == RepeatMode::One) {
        m_channel->setMode(FMOD_LOOP_NORMAL);
        m_channel->setLoopCount(-1);
    } else {
        m_channel->setMode(FMOD_LOOP_OFF);
    }
}

void EditorMusicPlayer::applyVolume() {
    float effective = m_muted ? 0.f : (m_volume * m_fadeLevel);
    if (m_group) {
        m_group->setVolume(effective);
    } else if (m_channel) {
        m_channel->setVolume(effective);
    }
}

bool EditorMusicPlayer::play(std::string const& trackId) {
    ensureQueue();

    auto const* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track || !playable(*track)) return false;

    if (!openFile(track->audioPath)) return false;

    m_trackId = trackId;
    m_paused = false;
    m_suspended = false;
    m_suspendedPosMs = 0;

    if (m_history.empty() || m_history.back() != trackId) {
        m_history.push_back(trackId);
        while (m_history.size() > kMaxHistory) m_history.pop_front();
    }

    auto found = std::find(m_queue.begin(), m_queue.end(), trackId);
    if (found != m_queue.end()) m_index = static_cast<std::size_t>(found - m_queue.begin());

    Mod::get()->setSavedValue<std::string>(kLastKey, trackId);
    notifyChanged();
    return true;
}

std::size_t EditorMusicPlayer::nextIndex() const {
    if (m_queue.size() <= 1) return 0;
    if (!m_shuffle) return (m_index + 1) % m_queue.size();

    auto pick = randomIndex(m_queue.size());
    if (pick == m_index) pick = (pick + 1) % m_queue.size();
    return pick;
}

bool EditorMusicPlayer::playNext() {
    if (!m_queuedNextId.empty()) {
        std::string nextId = std::move(m_queuedNextId);
        m_queuedNextId.clear();
        if (play(nextId)) return true;
    }
    if (!ensureQueue()) return false;
    return play(m_queue[nextIndex()]);
}

bool EditorMusicPlayer::playPrevious() {
    if (!ensureQueue()) return false;

    if (positionMs() > 3000) {
        seekMs(0);
        return true;
    }
    if (m_history.size() >= 2) {
        m_history.pop_back();
        std::string prevId = m_history.back();
        if (auto const* t = MenuMusicLibrary::get().findTrack(prevId); t && playable(*t)) {
            return play(prevId);
        }
    }
    auto index = m_index == 0 ? m_queue.size() - 1 : m_index - 1;
    return play(m_queue[index]);
}

void EditorMusicPlayer::togglePause() {
    setPaused(!m_paused);
}

void EditorMusicPlayer::setPaused(bool paused) {
    if (m_suspended) return;
    if (!m_channel) {
        if (!paused) {
            if (!m_trackId.empty()) play(m_trackId);
            else playNext();
        }
        return;
    }
    if (m_paused == paused) return;

    m_paused = paused;
    if (paused) {
        m_pendingPauseAfterFade = true;
        m_fadeTarget = 0.f;
    } else {
        m_pendingPauseAfterFade = false;
        m_channel->setPaused(false);
        m_fadeTarget = 1.f;
    }
    notifyChanged();
}

void EditorMusicPlayer::stop() {
    releaseSound();
    m_trackId.clear();
    m_paused = false;
    m_suspended = false;
    m_pendingPauseAfterFade = false;
    m_suspendedPosMs = 0;
    m_fadeLevel = 1.f;
    m_fadeTarget = 1.f;
    notifyChanged();
}

void EditorMusicPlayer::seekMs(int ms) {
    if (!m_channel) return;
    int target = std::max(0, ms);
    if (auto length = lengthMs(); length > 0) target = std::min(target, std::max(0, length - 200));
    m_channel->setPosition(static_cast<unsigned int>(target), FMOD_TIMEUNIT_MS);
}

bool EditorMusicPlayer::playLevelSong(GJGameLevel* level, std::string* outStatus) {
    if (!level) {
        if (outStatus) *outStatus = "No hay nivel activo.";
        return false;
    }
    int songId = level->m_songID;
    if (songId <= 0) {
        if (outStatus) *outStatus = "Este nivel usa una cancion oficial de GD (no descargable como archivo).";
        return false;
    }

    std::string expectedId = fmt::format("gd_{}", songId);
    auto& lib = MenuMusicLibrary::get();
    lib.load();

    if (auto const* existing = lib.findTrack(expectedId); existing && playable(*existing)) {
        if (play(existing->id)) return true;
    }

    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm) {
        if (outStatus) *outStatus = "MusicDownloadManager no disponible.";
        return false;
    }
    auto path = mdm->pathForSong(songId);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(paimon::assets::pathFromUtf8(path), ec) || ec) {
        if (outStatus) *outStatus = fmt::format("La cancion #{} no esta descargada aun.", songId);
        return false;
    }

    lib.syncDownloadedSongs(true);
    refreshQueue();
    if (play(expectedId)) return true;

    if (outStatus) *outStatus = fmt::format("No se pudo reproducir la cancion #{}.", songId);
    return false;
}

void EditorMusicPlayer::setVolume(float value, bool persist) {
    float const clamped = std::clamp(value, 0.f, 1.f);
    bool muteChanged = false;
    if (clamped > 0.001f && m_muted) {
        m_muted = false;
        muteChanged = true;
    }
    bool const volChanged = std::abs(clamped - m_volume) > 0.0005f;
    m_volume = clamped;
    if (volChanged || muteChanged) {
        applyVolume();
    }
    if (persist) {
        if (muteChanged) Mod::get()->setSavedValue<bool>(kMutedKey, false);
        Mod::get()->setSavedValue<double>(kVolumeKey, m_volume);
        notifyChanged();
    } else if (muteChanged) {
        Mod::get()->setSavedValue<bool>(kMutedKey, false);
        notifyChanged();
    }
}

void EditorMusicPlayer::toggleMute() {
    m_muted = !m_muted;
    Mod::get()->setSavedValue<bool>(kMutedKey, m_muted);
    applyVolume();
    notifyChanged();
}

void EditorMusicPlayer::toggleShuffle() {
    m_shuffle = !m_shuffle;
    Mod::get()->setSavedValue<bool>(kShuffleKey, m_shuffle);
    notifyChanged();
}

void EditorMusicPlayer::cycleRepeat() {
    switch (m_repeat) {
        case RepeatMode::Off: m_repeat = RepeatMode::All; break;
        case RepeatMode::All: m_repeat = RepeatMode::One; break;
        case RepeatMode::One: m_repeat = RepeatMode::Off; break;
    }
    Mod::get()->setSavedValue<int64_t>(kRepeatKey, static_cast<int64_t>(m_repeat));
    applyLoopMode();
    notifyChanged();
}

bool EditorMusicPlayer::isPlaying() const {
    return !m_paused && !m_suspended && channelAlive();
}

int EditorMusicPlayer::positionMs() const {
    if (m_suspended && m_playtestBehavior == PlaytestBehavior::PauseAndResume) {
        return m_suspendedPosMs;
    }
    if (!m_channel) return 0;
    unsigned int pos = 0;
    if (m_channel->getPosition(&pos, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0;
    return static_cast<int>(pos);
}

int EditorMusicPlayer::lengthMs() const {
    if (!m_sound) return 0;
    unsigned int len = 0;
    if (m_sound->getLength(&len, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0;
    return static_cast<int>(len);
}

float EditorMusicPlayer::outputPeak() const {
    if (!m_meterDsp || !isPlaying()) return 0.f;
    FMOD_DSP_METERING_INFO output{};
    if (m_meterDsp->getMeteringInfo(nullptr, &output) != FMOD_OK) return 0.f;
    float peak = 0.f;
    for (short i = 0; i < output.numchannels; ++i) {
        peak = std::max(peak, output.peaklevel[i]);
    }
    return std::clamp(peak, 0.f, 1.f);
}

void EditorMusicPlayer::suspend() {
    if (m_suspended || !m_channel) return;
    if (m_playtestBehavior == PlaytestBehavior::KeepPlaying) return;

    m_suspended = true;
    if (m_playtestBehavior == PlaytestBehavior::DuckVolume) {
        m_fadeTarget = kDuckFade;
        applyVolume();
        return;
    }

    m_suspendedPosMs = positionMs();
    m_pendingPauseAfterFade = false;
    m_channel->setPaused(true);
}

void EditorMusicPlayer::resumeFromSuspend() {
    if (!m_suspended) return;
    m_suspended = false;

    if (m_playtestBehavior == PlaytestBehavior::DuckVolume) {
        m_fadeTarget = m_paused ? 0.f : 1.f;
        applyVolume();
        return;
    }

    if (!channelAlive()) {
        m_channel = nullptr;
        if (m_trackId.empty()) return;
        auto pos = m_suspendedPosMs;
        if (play(m_trackId)) seekMs(pos);
        return;
    }
    if (!m_paused) {
        m_fadeLevel = 0.1f;
        m_fadeTarget = 1.f;
        applyVolume();
        m_channel->setPaused(false);
    }
}

void EditorMusicPlayer::setFxConfig(EditorFxConfig cfg) {
    normalizeFx(cfg);
    m_fx = cfg;
    auto* mod = Mod::get();
    mod->setSavedValue<bool>(kFxEnabledKey, m_fx.enabled);
    mod->setSavedValue<int64_t>(kFxPresetKey, static_cast<int64_t>(m_fx.preset));
    mod->setSavedValue<double>(kFxSpeedKey, m_fx.speed);
    mod->setSavedValue<double>(kFxBassKey, m_fx.bassDb);
    mod->setSavedValue<double>(kFxTrebleKey, m_fx.trebleDb);
    mod->setSavedValue<double>(kFxLowpassKey, m_fx.lowpassHz);
    mod->setSavedValue<double>(kFxReverbKey, m_fx.reverbMix);
    mod->setSavedValue<double>(kFxEchoKey, m_fx.echoMix);
    applyFxToGroup();
    notifyChanged();
}

void EditorMusicPlayer::applyFxPreset(EditorFxPreset preset) {
    setFxConfig(fxPresetConfig(preset));
}

void EditorMusicPlayer::setFxBypassed(bool bypassed) {
    if (m_fxBypassed == bypassed) return;
    m_fxBypassed = bypassed;
    applyFxToGroup();
    notifyChanged();
}

void EditorMusicPlayer::setPlaytestBehavior(PlaytestBehavior behavior) {
    m_playtestBehavior = behavior;
    Mod::get()->setSavedValue<int64_t>(kPlaytestKey, static_cast<int64_t>(behavior));
    notifyChanged();
}

void EditorMusicPlayer::setAutoPlayOnEnter(bool enabled) {
    m_autoPlayOnEnter = enabled;
    Mod::get()->setSavedValue<bool>(kAutoPlayKey, enabled);
    notifyChanged();
}

void EditorMusicPlayer::setShowToastOnChange(bool enabled) {
    m_showToastOnChange = enabled;
    Mod::get()->setSavedValue<bool>(kToastKey, enabled);
    notifyChanged();
}

void EditorMusicPlayer::setAnimationsEnabled(bool enabled) {
    m_animationsEnabled = enabled;
    Mod::get()->setSavedValue<bool>(kAnimKey, enabled);
    notifyChanged();
}

void EditorMusicPlayer::setPanelSkin(int skin) {
    m_panelSkin = std::clamp(skin, 0, 3);
    Mod::get()->setSavedValue<int64_t>(kSkinKey, m_panelSkin);
    notifyChanged();
}

void EditorMusicPlayer::tick(float dt) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    float const step = std::max(dt, 0.f) * 6.5f;
    if (std::abs(m_fadeLevel - m_fadeTarget) > 0.005f) {
        if (m_fadeLevel < m_fadeTarget) {
            m_fadeLevel = std::min(m_fadeTarget, m_fadeLevel + step);
        } else {
            m_fadeLevel = std::max(m_fadeTarget, m_fadeLevel - step);
        }
        applyVolume();
    } else if (m_fadeLevel != m_fadeTarget) {
        m_fadeLevel = m_fadeTarget;
        applyVolume();
    }

    if (m_pendingPauseAfterFade && m_fadeLevel <= 0.01f) {
        m_pendingPauseAfterFade = false;
        if (m_channel && m_paused) {
            m_channel->setPaused(true);
        }
    }

    if ((m_suspended && m_playtestBehavior == PlaytestBehavior::PauseAndResume) || m_paused || m_trackId.empty()) {
        return;
    }
    if (!m_channel || channelAlive()) return;

    // Channel ended naturally: advance queue.
    m_channel = nullptr;
    if (m_repeat == RepeatMode::Off && m_index + 1 >= m_queue.size() && !m_shuffle && m_queuedNextId.empty()) {
        stop();
        return;
    }
    playNext();
}

void EditorMusicPlayer::shutdown() {
    m_shuttingDown = true;
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    releaseSound();
    detachDsps();
    if (m_group) {
        m_group->release();
        m_group = nullptr;
    }
}

std::size_t EditorMusicPlayer::addListener(StateListener cb) {
    auto token = m_nextListenerToken++;
    m_listeners.emplace_back(token, std::move(cb));
    return token;
}

void EditorMusicPlayer::removeListener(std::size_t token) {
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](auto const& p) { return p.first == token; }),
        m_listeners.end());
}

void EditorMusicPlayer::notifyChanged() {
    auto copy = m_listeners;
    for (auto& [token, cb] : copy) {
        if (cb) cb();
    }
}

} // namespace paimon::editormusic
