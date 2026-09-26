#pragma once

// popup behind the "Notices" button: toggles the new-request notice and
// previews it on a fake screen (spot, size, seconds) before it hits the stream.

#include <Geode/Geode.hpp>

#include "../TwitchRequestNotify.hpp"

namespace paimon::twitch {

class TwitchNotifyPopup : public geode::Popup {
public:
    static TwitchNotifyPopup* create();

protected:
    bool init() override;

    void buildPreview(cocos2d::CCPoint origin, cocos2d::CCSize size);
    void buildOptions(cocos2d::CCPoint origin, cocos2d::CCSize size);

    // rebuilds the preview card (content change).
    void rebuildCard(bool replayEnter);
    // only repositions and refreshes texts (spot or size change).
    void syncCard(bool replayEnter);
    void replayExit();
    cocos2d::CCPoint cardRestPoint() const;
    void apply(std::function<void(NotifyConfig&)> const& change, bool rebuild, bool replay);
    void onTest();

    NotifyConfig m_config;
    cocos2d::CCNode* m_screen = nullptr;
    cocos2d::CCNodeRGBA* m_card = nullptr;
    cocos2d::CCLabelBMFont* m_spotLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_animLabel = nullptr;
    // fake screen size relative to the real one.
    float m_ratio = 1.f;
    float m_infoWidth = 240.f;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::twitch
