#pragma once

#include <Geode/Geode.hpp>

#include "../services/MenuMusicEffects.hpp"

namespace paimon::menumusic {

class MenuMusicSettingsPopup : public geode::Popup {
public:
    static MenuMusicSettingsPopup* create();
    ~MenuMusicSettingsPopup() override;

protected:
    bool init() override;

    void rebuild();
    void scheduleRebuild();
    void persist();
    void persistSpatial();

private:
    MusicEffectsConfig m_cfg;
    int m_tab = 0;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::menumusic
