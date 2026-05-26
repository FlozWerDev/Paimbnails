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

// Tracks whether we are currently inside a CCNode/CCScene destructor chain
// triggered by CCDirector::setNextScene. While this flag is set,
// syncAudioInteropFlags() must not touch the running scene because
// CCDirector::getRunningScene() returns a pointer to the scene that is
// being released (its CCNode members, including m_pUserObject, are in
// a partially destroyed state). Reading them causes an access violation
// inside CCNode::setUserFlag (via GeodeNodeMetadata::set).
inline std::atomic<bool>& interopSceneTeardownGuard() {
    static std::atomic<bool> inTeardown{false};
    return inTeardown;
}

struct InteropSceneTeardownScope {
    InteropSceneTeardownScope() { interopSceneTeardownGuard().store(true, std::memory_order_release); }
    ~InteropSceneTeardownScope() { interopSceneTeardownGuard().store(false, std::memory_order_release); }
};

inline void syncAudioInteropFlags() {
    // Always update the atomic-only state above; this function only mirrors
    // those atomics into the running scene's user flags. If we cannot safely
    // touch the scene, the atomic state is still authoritative.
    if (interopSceneTeardownGuard().load(std::memory_order_acquire)) {
        return;
    }

    auto* director = cocos2d::CCDirector::get();
    if (!director) {
        return;
    }

    // While CCDirector::setNextScene is running, the previous running scene
    // is being released and its sub-tree is mid-destruction. Touching
    // m_pRunningScene here will read partially-destroyed CCNode members
    // (specifically m_pUserObject at offset 0x78 on Windows x64) and crash
    // inside CCNode::setUserFlag. Detect the transition via getNextScene()
    // / isSendCleanupToScene() and skip the user-flag mirror in that case.
    if (director->getNextScene() != nullptr || director->isSendCleanupToScene()) {
        return;
    }

    auto* scene = director->getRunningScene();
    if (!scene) {
        return;
    }

    // Defensive: if the scene's retain count is 0 it is being destroyed
    // (this catches the recursive ~CCNode case that follows release()).
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
