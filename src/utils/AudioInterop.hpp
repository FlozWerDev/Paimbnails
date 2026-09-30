#pragma once

#include <Geode/Geode.hpp>
#include <atomic>

namespace paimon {

inline std::string const& audioOwnedFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/audio-owned";
    return flag;
}

inline std::string const& profileMusicFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/profile-music-active";
    return flag;
}

inline std::string const& dynamicSongFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/dynamic-song-active";
    return flag;
}

inline std::atomic<bool>& profileMusicInteropState() {
    static std::atomic<bool> active{false};
    return active;
}

inline std::atomic<bool>& dynamicSongInteropState() {
    static std::atomic<bool> active{false};
    return active;
}

inline std::atomic<bool>& videoAudioInteropState() {
    static std::atomic<bool> active{false};
    return active;
}

// during teardown the scene is mid-destruction, don't touch it.
inline std::atomic<bool>& interopSceneTeardownGuard() {
    static std::atomic<bool> inTeardown{false};
    return inTeardown;
}

struct InteropSceneTeardownScope {
    InteropSceneTeardownScope() { interopSceneTeardownGuard().store(true, std::memory_order_release); }
    ~InteropSceneTeardownScope() { interopSceneTeardownGuard().store(false, std::memory_order_release); }
};

inline void syncAudioInteropFlags() {
    // mirror atomics only; skip if the scene can't be touched.
    if (interopSceneTeardownGuard().load(std::memory_order_acquire)) {
        return;
    }

    auto* director = cocos2d::CCDirector::get();
    if (!director) {
        return;
    }

    // scene transition in progress: previous scene is being released.
    if (director->getNextScene() != nullptr || director->isSendCleanupToScene()) {
        return;
    }

    auto* scene = director->getRunningScene();
    if (!scene) {
        return;
    }

    // if retain is 0 the scene is being destroyed.
    if (scene->retainCount() <= 0) {
        return;
    }

    bool profileActive = profileMusicInteropState();
    bool dynamicActive = dynamicSongInteropState();
    bool videoActive   = videoAudioInteropState();
    scene->setUserFlag(profileMusicFlag(), profileActive);
    scene->setUserFlag(dynamicSongFlag(), dynamicActive);
    scene->setUserFlag(audioOwnedFlag(), profileActive || dynamicActive || videoActive);
}

inline void setProfileMusicInteropActive(bool active) {
    profileMusicInteropState() = active;
    syncAudioInteropFlags();
}

inline void setDynamicSongInteropActive(bool active) {
    dynamicSongInteropState() = active;
    syncAudioInteropFlags();
}

inline bool isProfileMusicInteropActive() {
    return profileMusicInteropState();
}

inline bool isDynamicSongInteropActive() {
    return dynamicSongInteropState();
}

inline bool isAudioOwnedByPaimon() {
    return profileMusicInteropState() || dynamicSongInteropState() || videoAudioInteropState();
}

inline void setVideoAudioInteropActive(bool active) {
    videoAudioInteropState() = active;
    syncAudioInteropFlags();
}

inline bool isVideoAudioInteropActive() {
    return videoAudioInteropState();
}

} // namespace paimon
