#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/Popup.hpp>
#include <string>

namespace paimon::scorecell {

class ScoreCellSettingsPopup : public geode::Popup {
public:
    static ScoreCellSettingsPopup* create();

    void setOnClose(geode::CopyableFunction<void()> cb) { m_onCloseCb = std::move(cb); }

protected:
    bool initContents();
    void onClose(cocos2d::CCObject* sender) override;

    void onToggleGradient(cocos2d::CCObject*);
    void onToggleHover(cocos2d::CCObject*);
    void onCycleEffect(cocos2d::CCObject*);
    void onCycleHover(cocos2d::CCObject*);
    void onCycleEntrance(cocos2d::CCObject*);
    void onSpeed(cocos2d::CCObject*);
    void onOpacity(cocos2d::CCObject*);
    void onIntensity(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);

    void rebuildPreview();
    void refreshLabels();

    geode::CopyableFunction<void()> m_onCloseCb;

    cocos2d::CCNode* m_previewContainer = nullptr;

    geode::Ref<cocos2d::CCNode> m_effectBtnSprite = nullptr;
    geode::Ref<cocos2d::CCNode> m_hoverBtnSprite = nullptr;
    geode::Ref<cocos2d::CCNode> m_entranceBtnSprite = nullptr;

    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;
};

}
