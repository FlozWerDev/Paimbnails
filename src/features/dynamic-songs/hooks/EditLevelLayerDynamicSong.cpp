#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/loader/Mod.hpp>

#include "../../audio/services/AudioContextCoordinator.hpp"

using namespace geode::prelude;

// reuse levelinfo audio context for dynamic song.
class $modify(PaimonDynamicSongEditLevelLayer, EditLevelLayer) {
    struct Fields {
        bool m_audioActivated = false;
    };

    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        this->scheduleOnce(schedule_selector(PaimonDynamicSongEditLevelLayer::forcePlayDynamic), 0.f);
        return true;
    }

    void forcePlayDynamic(float) {
        if (m_fields->m_audioActivated || !this->getParent() || !m_level) return;
        if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;

        m_fields->m_audioActivated = true;
        AudioContextCoordinator::get().activateLevelInfo(m_level, true);
    }

    void deactivateDynamic() {
        this->unschedule(schedule_selector(PaimonDynamicSongEditLevelLayer::forcePlayDynamic));
        if (m_fields->m_audioActivated) {
            m_fields->m_audioActivated = false;
            AudioContextCoordinator::get().deactivateLevelInfo(false);
        }
    }

    $override
    void onBack(CCObject* sender) {
        deactivateDynamic();
        EditLevelLayer::onBack(sender);
    }

    $override
    void onPlay(CCObject* sender) {
        deactivateDynamic();
        EditLevelLayer::onPlay(sender);
    }
};
