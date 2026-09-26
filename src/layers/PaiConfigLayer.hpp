#pragma once

// single-screen background editor with live preview under a vanilla UI mock.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>

#include <string>
#include <vector>

#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace paimon::bgpreview {
class LayerPreviewNode;
}

class PaiConfigLayer : public cocos2d::CCLayer {
public:
    static PaiConfigLayer* create();
    static cocos2d::CCScene* scene();

    // overlay entry point for callers inside a popup that cannot push a scene.
    static PaiConfigLayer* openOverlay();

protected:
    bool init() override;
    void keyBackClicked() override;

    void buildChrome();
    void buildBackgroundsTab();
    void buildProfileTab();
    void buildExtrasTab();

    cocos2d::CCNode* buildScreenList(cocos2d::CCRect area);
    cocos2d::CCNode* buildPreviewCard(cocos2d::CCRect area);
    cocos2d::CCNode* buildControlsCard(cocos2d::CCRect area);

    // control rows, all anchored at {0,0}.
    cocos2d::CCNode* rowSources(float width);
    cocos2d::CCNode* rowLevelId(float width);
    cocos2d::CCNode* rowDarken(float width);
    cocos2d::CCNode* rowFilter(float width);
    cocos2d::CCNode* rowAdaptive(float width);
    cocos2d::CCNode* rowVideoSettings(float width);
    cocos2d::CCNode* rowCopyToAll(float width);
    cocos2d::CCNode* rowModuleWarning(float width);

    void switchTab(int index);
    void selectScreen(std::string const& key);
    void refreshAll();
    void refreshScreenList();
    void refreshPreview();

    LayerBgConfig currentConfig() const;
    void mutateConfig(std::function<void(LayerBgConfig&)> const& fn,
                      char const* toastKey = nullptr, char const* toastFallback = nullptr);

    void onPickImage();
    void onPickVideo();
    void onUseShaderBg();
    void onUseRandom();
    void onUseDynamicShader();
    void onUseLevelId();
    void onUseSameAs();
    void onUseDefault();
    void onVideoSettings();
    void onToggleDark(bool on);
    void onDarkSlider(cocos2d::CCObject*);
    void onFilterStep(int delta);
    void onFilterSlider(cocos2d::CCObject*);
    void onToggleAdaptive(bool on);
    void onToggleMock();
    void onExpandPreview();
    void onCopyToAllScreens();
    void onApplyAndRestart();
    void onResetScreen();
    void onBack();

    void rebuildProfilePreview();
    void onProfileImage();
    void onProfileClear();
    void onProfileShape();
    void onClearAllCache();

    cocos2d::CCNode* makeCardWindow(cocos2d::CCRect area, char const* title);
    cocos2d::CCNode* addScrollHint(cocos2d::CCNode* card, cocos2d::CCSize cardSize);
    void updateFilterLabels();
    std::vector<std::pair<std::string, std::string>> const& activeFilterList() const;

    cocos2d::CCMenu* m_chromeMenu = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;
    std::vector<cocos2d::CCNode*> m_tabPages;
    int m_currentTab = 0;

    std::string m_selectedKey = "menu";

    geode::ScrollLayer* m_screenScroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_screenButtons;

    paimon::bgpreview::LayerPreviewNode* m_preview = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_screenChipLabel = nullptr;
    cocos2d::CCLabelBMFont* m_blockedLabel = nullptr;
    cocos2d::CCNode* m_blockedOverlay = nullptr;
    CCMenuItemToggler* m_mockToggle = nullptr;

    geode::ScrollLayer* m_controlsScroll = nullptr;
    cocos2d::CCNode* m_controlsHint = nullptr;
    // rows live in Refs: the scroll layer culls with setVisible(),
    // so visibility does not track config intent.
    std::vector<geode::Ref<cocos2d::CCNode>> m_controlRows;
    // packs visible rows gap-free; scrolls to top only when the set changes.
    void relayoutControls();
    bool isControlRowEnabled(cocos2d::CCNode* row) const;
    std::string m_controlsSignature;
    geode::TextInput* m_levelIdInput = nullptr;
    CCMenuItemToggler* m_darkToggle = nullptr;
    Slider* m_darkSlider = nullptr;
    cocos2d::CCLabelBMFont* m_darkValueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_filterLabel = nullptr;
    Slider* m_filterSlider = nullptr;
    cocos2d::CCLabelBMFont* m_filterValueLabel = nullptr;
    CCMenuItemToggler* m_adaptiveToggle = nullptr;
    geode::Ref<cocos2d::CCNode> m_adaptiveRow;
    geode::Ref<cocos2d::CCNode> m_videoRow;
    geode::Ref<cocos2d::CCNode> m_moduleWarnRow;
    // conditional rows: wanted by config, not drawn state.
    bool m_showAdaptive = false;
    bool m_showVideo = false;
    bool m_showModuleWarn = false;
    cocos2d::CCLabelBMFont* m_filterTitle = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_sourceButtons;

    cocos2d::CCNode* m_profilePreview = nullptr;
    int m_profilePreviewGen = 0;

    int m_filterIndex = 0;
    bool m_overlayMode = false;

    float m_controlsTargetY = 0.f;
    bool m_controlsTargetSet = false;
    float m_screenTargetY = 0.f;
    bool m_screenTargetSet = false;
    void scrollWheel(float x, float y) override;
    void update(float dt) override;
};
