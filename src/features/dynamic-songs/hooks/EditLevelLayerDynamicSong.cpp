#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/loader/Mod.hpp>

#include "../../audio/services/AudioContextCoordinator.hpp"
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

// Plays the level's song on the EditLevelLayer info screen, reusing the same
// LevelInfo audio context that already drives dynamic songs everywhere else.
class $modify(PaimonDynamicSongEditLevelLayer, EditLevelLayer) {
    struct Fields {
        bool m_audioActivated = false;
    };

    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        return true;
    }

    $override
    void onEnterTransitionDidFinish() {
        EditLevelLayer::onEnterTransitionDidFinish();
        this->unschedule(schedule_selector(PaimonDynamicSongEditLevelLayer::forcePlayDynamic));
        this->scheduleOnce(schedule_selector(PaimonDynamicSongEditLevelLayer::forcePlayDynamic), 0.f);
    }

    void forcePlayDynamic(float) {
        if (m_fields->m_audioActivated || !this->getParent() || !m_level) return;
        if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;

        m_fields->m_audioActivated = true;
        AudioContextCoordinator::get().activateLevelInfo(m_level, true);
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonDynamicSongEditLevelLayer::forcePlayDynamic));
        if (m_fields->m_audioActivated) {
            m_fields->m_audioActivated = false;
            AudioContextCoordinator::get().deactivateLevelInfo(false);
        }
        EditLevelLayer::onExit();
    }
};
