#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../services/TransitionManager.hpp"

class StingerConfigPopup : public PaimonPopup {
public:
    static StingerConfigPopup* create(TransitionConfig config, std::function<void(TransitionConfig)> save);
    void update(float dt) override;
private:
    bool init(TransitionConfig config, std::function<void(TransitionConfig)> save);
    void refresh();
    void showIdle();
    void import();
    void preview();
    TransitionConfig m_config;
    std::function<void(TransitionConfig)> m_save;
    std::shared_ptr<paimon::transitions::TransitionMedia> m_media;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_timing = nullptr;
    cocos2d::CCLayerColor* m_preview = nullptr;
    cocos2d::CCNode* m_split = nullptr;
    cocos2d::CCLayerColor* m_fullA = nullptr;
    cocos2d::CCLayerColor* m_fullB = nullptr;
    cocos2d::CCLayerColor* m_progress = nullptr;
    cocos2d::CCLayerColor* m_cutMark = nullptr;
    cocos2d::CCLabelBMFont* m_stateTag = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    cocos2d::CCLabelBMFont* m_cutVal = nullptr;
    cocos2d::CCLabelBMFont* m_durVal = nullptr;
    cocos2d::CCSprite* m_overlay = nullptr;
    float m_elapsed = 0.f;
    bool m_busy = false, m_playing = false;
};
