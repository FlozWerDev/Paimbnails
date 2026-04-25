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

inline void syncAudioInteropFlags() {
    auto* director = cocos2d::CCDirector::sharedDirector();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) {
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