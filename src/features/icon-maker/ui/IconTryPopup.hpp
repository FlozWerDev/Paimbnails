#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <string>
#include <vector>

class SimplePlayer;

namespace paimon::icon_maker {

class IconTryPopup : public PaimonPopup {
public:
    static IconTryPopup* create(IconProject project);

protected:
    struct RenderedZone {
        std::string slotKey;
        int part = 0;
        geode::Ref<cocos2d::CCTexture2D> texture;
    };

    bool init(IconProject project);

    void kickRender();
    void rebuildPreview();
    void refreshControls();

    IconProject m_project;
    std::vector<RenderedZone> m_zones;

    cocos2d::CCNode* m_stage = nullptr;
    cocos2d::CCNode* m_controlsHost = nullptr;
    cocos2d::CCLayerColor* m_stageBg = nullptr;
    cocos2d::CCSprite* m_stageChecker = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    SimplePlayer* m_vanilla = nullptr;

    bool m_exactColors = true;
    bool m_showGlow = true;
    int m_backgroundMode = 0;
    int m_sizeIndex = 1;
    bool m_busy = true;
};

}  // namespace paimon::icon_maker
