#include <Geode/modify/AudioAssetsBrowser.hpp>

#include "../services/DynamicSongManager.hpp"

using namespace geode::prelude;

class $modify(PaimonAudioAssetsBrowser, AudioAssetsBrowser) {
    struct Fields {
        bool m_resumeDynamicSong = false;
    };

    bool takeResumeFlag() {
        bool const resume = m_fields->m_resumeDynamicSong;
        m_fields->m_resumeDynamicSong = false;
        return resume;
    }

    static void resumeIfNeeded(bool resume) {
        if (resume) DynamicSongManager::get()->resumeSuspendedPlayback();
    }

    bool init(gd::vector<int>& songIds, gd::vector<int>& sfxIds) {
        if (!AudioAssetsBrowser::init(songIds, sfxIds)) return false;

        auto* dynamic = DynamicSongManager::get();
        if (dynamic->isActive() && dynamic->isInValidLayer()) {
            dynamic->suspendPlaybackForExternalAudio();
            m_fields->m_resumeDynamicSong = dynamic->hasSuspendedPlayback();
        }
        return true;
    }

    void onClose(CCObject* sender) {
        bool const resume = takeResumeFlag();
        AudioAssetsBrowser::onClose(sender);
        resumeIfNeeded(resume);
    }

    $override
    void keyBackClicked() {
        bool const resume = takeResumeFlag();
        AudioAssetsBrowser::keyBackClicked();
        resumeIfNeeded(resume);
    }
};
