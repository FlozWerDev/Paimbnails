#include "EditorMusicPlayer.hpp"

#include "../../menu-music/services/MenuMusicLibrary.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <filesystem>
#include <random>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;
using paimon::menumusic::MusicTrack;

namespace paimon::editormusic {

namespace {

constexpr char const* kVolumeKey  = "editor-music-volume";
constexpr char const* kShuffleKey = "editor-music-shuffle";
constexpr char const* kRepeatKey  = "editor-music-repeat";
constexpr char const* kLastKey    = "editor-music-last";

bool playable(MusicTrack const& track) {
    if (track.audioPath.empty() || track.blacklisted) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(track.audioPath, ec) && !ec;
}

std::size_t randomIndex(std::size_t count) {
    static std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng);
}

} // namespace

EditorMusicPlayer& EditorMusicPlayer::get() {
    static EditorMusicPlayer instance;
    return instance;
}

EditorMusicPlayer::EditorMusicPlayer() {
    auto* mod = Mod::get();
    m_volume = std::clamp(static_cast<float>(mod->getSavedValue<double>(kVolumeKey, 0.6)), 0.f, 1.f);
    m_shuffle = mod->getSavedValue<bool>(kShuffleKey, false);
    auto repeat = mod->getSavedValue<int64_t>(kRepeatKey, 1);
    m_repeat = repeat == 0 ? RepeatMode::Off : (repeat == 2 ? RepeatMode::One : RepeatMode::All);
}

void EditorMusicPlayer::refreshQueue() {
    auto& lib = MenuMusicLibrary::get();
    lib.load();

    m_queue.clear();
    for (auto const& track : lib.tracks()) {
        if (playable(track)) m_queue.push_back(track.id);
    }

    // Nothing queued yet: preselect last session's track so the play button
    // has something to start without going through the picker first.
    if (m_trackId.empty()) {
        auto last = Mod::get()->getSavedValue<std::string>(kLastKey, "");
        if (std::find(m_queue.begin(), m_queue.end(), last) != m_queue.end()) m_trackId = last;
    }

    auto found = std::find(m_queue.begin(), m_queue.end(), m_trackId);
    m_index = found != m_queue.end() ? static_cast<std::size_t>(found - m_queue.begin()) : 0;
}

std::string EditorMusicPlayer::trackName() const {
    if (m_trackId.empty()) return "";
    auto const* track = MenuMusicLibrary::get().findTrack(m_trackId);
    if (!track) return "";
    if (!track->displayName.empty()) return track->displayName;
    return utils::string::pathToString(std::filesystem::path(track->audioPath).stem());
}

bool EditorMusicPlayer::openFile(std::string const& path) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    releaseSound();

    auto result = engine->m_system->createSound(
        path.c_str(), FMOD_CREATESTREAM | FMOD_2D | FMOD_LOOP_OFF, nullptr, &m_sound);
    if (result != FMOD_OK || !m_sound) {
        log::warn("[EditorMusic] createSound failed ({}) for '{}'", static_cast<int>(result), path);
        m_sound = nullptr;
        return false;
    }

    // Started paused so volume and loop mode land before the first sample.
    result = engine->m_system->playSound(m_sound, nullptr, true, &m_channel);
    if (result != FMOD_OK || !m_channel) {
        log::warn("[EditorMusic] playSound failed ({})", static_cast<int>(result));
        releaseSound();
        return false;
    }

    applyLoopMode();
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
    if (m_channel) m_channel->setVolume(m_volume);
}

bool EditorMusicPlayer::play(std::string const& trackId) {
    if (m_queue.empty()) refreshQueue();

    auto const* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track || !playable(*track)) return false;

    if (!openFile(track->audioPath)) return false;

    m_trackId = trackId;
    m_paused = false;
    m_suspended = false;
    m_suspendedPosMs = 0;

    auto found = std::find(m_queue.begin(), m_queue.end(), trackId);
    if (found != m_queue.end()) m_index = static_cast<std::size_t>(found - m_queue.begin());

    Mod::get()->setSavedValue<std::string>(kLastKey, trackId);
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
    if (m_queue.empty()) refreshQueue();
    if (m_queue.empty()) return false;
    return play(m_queue[nextIndex()]);
}

bool EditorMusicPlayer::playPrevious() {
    if (m_queue.empty()) refreshQueue();
    if (m_queue.empty()) return false;

    // Restart the track first, like every other player does.
    if (positionMs() > 3000) {
        seekMs(0);
        return true;
    }
    auto index = m_index == 0 ? m_queue.size() - 1 : m_index - 1;
    return play(m_queue[index]);
}

void EditorMusicPlayer::togglePause() {
    if (m_suspended) return;
    if (!m_channel) {
        if (!m_trackId.empty()) play(m_trackId);
        else playNext();
        return;
    }
    m_paused = !m_paused;
    m_channel->setPaused(m_paused);
}

void EditorMusicPlayer::stop() {
    releaseSound();
    m_trackId.clear();
    m_paused = false;
    m_suspended = false;
    m_suspendedPosMs = 0;
}

void EditorMusicPlayer::seekMs(int ms) {
    if (!m_channel) return;
    int target = std::max(0, ms);
    if (auto length = lengthMs(); length > 0) target = std::min(target, std::max(0, length - 200));
    m_channel->setPosition(static_cast<unsigned int>(target), FMOD_TIMEUNIT_MS);
}

void EditorMusicPlayer::skipMs(int delta) {
    seekMs(positionMs() + delta);
}

void EditorMusicPlayer::setVolume(float value) {
    m_volume = std::clamp(value, 0.f, 1.f);
    applyVolume();
    Mod::get()->setSavedValue<double>(kVolumeKey, m_volume);
}

void EditorMusicPlayer::toggleShuffle() {
    m_shuffle = !m_shuffle;
    Mod::get()->setSavedValue<bool>(kShuffleKey, m_shuffle);
}

void EditorMusicPlayer::cycleRepeat() {
    switch (m_repeat) {
        case RepeatMode::Off: m_repeat = RepeatMode::All; break;
        case RepeatMode::All: m_repeat = RepeatMode::One; break;
        case RepeatMode::One: m_repeat = RepeatMode::Off; break;
    }
    Mod::get()->setSavedValue<int64_t>(kRepeatKey, static_cast<int64_t>(m_repeat));
    applyLoopMode();
}

bool EditorMusicPlayer::isPlaying() const {
    return !m_paused && !m_suspended && channelAlive();
}

int EditorMusicPlayer::positionMs() const {
    if (m_suspended) return m_suspendedPosMs;
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

void EditorMusicPlayer::suspend() {
    if (m_suspended || !m_channel) return;
    m_suspendedPosMs = positionMs();
    m_suspended = true;
    m_channel->setPaused(true);
}

void EditorMusicPlayer::resumeFromSuspend() {
    if (!m_suspended) return;
    m_suspended = false;

    if (!channelAlive()) {
        // The channel died while the playtest ran; reopen where we left off.
        m_channel = nullptr;
        if (m_trackId.empty()) return;
        auto pos = m_suspendedPosMs;
        if (play(m_trackId)) seekMs(pos);
        return;
    }
    if (!m_paused) m_channel->setPaused(false);
}

void EditorMusicPlayer::tick() {
    if (m_suspended || m_paused || m_trackId.empty()) return;
    if (!m_channel || channelAlive()) return;

    // The channel went quiet on its own: the track finished.
    m_channel = nullptr;
    if (m_repeat == RepeatMode::Off && m_index + 1 >= m_queue.size() && !m_shuffle) {
        stop();
        return;
    }
    playNext();
}

} // namespace paimon::editormusic
