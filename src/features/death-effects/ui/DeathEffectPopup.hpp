#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../services/OpenverseCatalog.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <vector>

class Slider;

namespace paimon::death_effects {

class DeathEffectPopup : public PaimonPopup {
public:
    static DeathEffectPopup* create();
    void onClose(cocos2d::CCObject* sender) override;

protected:
    bool init() override;

private:
    void buildHeader();
    void buildLibraryTab(cocos2d::CCNode* tab);
    void buildOnlineTab(cocos2d::CCNode* tab);
    void buildFxTab(cocos2d::CCNode* tab);
    void buildRulesTab(cocos2d::CCNode* tab);

    void rebuildList();
    void rebuildOnlineList();
    void refreshControls();
    void refreshStatus();

    void onTabSwitch(cocos2d::CCObject* sender);
    void onFilterChanged(std::string const& text);
    void onOnlineSearchChanged(std::string const& text);
    void onSearchOnline(cocos2d::CCObject* sender);
    void onQuickTag(cocos2d::CCObject* sender);
    void onDownloadOnline(cocos2d::CCObject* sender);
    void onPreviewOnline(cocos2d::CCObject* sender);
    void onOpenSourceOnline(cocos2d::CCObject* sender);

    void onToggleSound(cocos2d::CCObject* sender);
    void onToggleClutchSound(cocos2d::CCObject* sender);
    void onPreview(cocos2d::CCObject* sender);
    void onStopAll(cocos2d::CCObject* sender);
    void onDelete(cocos2d::CCObject* sender);
    void onOpenCredit(cocos2d::CCObject* sender);
    void onImportFile(cocos2d::CCObject* sender);
    void onImportFolder(cocos2d::CCObject* sender);
    void onOpenFolder(cocos2d::CCObject* sender);
    void onSelectAll(cocos2d::CCObject* sender);
    void onUseOriginal(cocos2d::CCObject* sender);
    void onCredits(cocos2d::CCObject* sender);
    void onTestSound(cocos2d::CCObject* sender);
    void onOpenVfxStudio(cocos2d::CCObject* sender);

    void onCycleDsp(cocos2d::CCObject* sender);
    void onDspMixChanged(cocos2d::CCObject* sender);
    void onCycleMaxDur(cocos2d::CCObject* sender);
    void onVolumeChanged(cocos2d::CCObject* sender);
    void onPitchChanged(cocos2d::CCObject* sender);
    void onSpeedChanged(cocos2d::CCObject* sender);
    void onPanChanged(cocos2d::CCObject* sender);

    void onCycleOrder(cocos2d::CCObject* sender);
    void onToggleAvoidRepeats(cocos2d::CCObject* sender);
    void onToggleStopOnReset(cocos2d::CCObject* sender);
    void onToggleAllowOverlap(cocos2d::CCObject* sender);
    void onToggleLayerVanilla(cocos2d::CCObject* sender);
    void onToggleClutchEnabled(cocos2d::CCObject* sender);
    void onToggleClutchNewBest(cocos2d::CCObject* sender);
    void onClutchThresholdChanged(cocos2d::CCObject* sender);

    static std::filesystem::path pathFromSender(cocos2d::CCObject* sender);

    int m_currentTab = 0;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    std::array<cocos2d::CCNode*, 4> m_tabNodes{};

    geode::ScrollLayer* m_scroll = nullptr;
    geode::ScrollLayer* m_onlineScroll = nullptr;
    geode::TextInput* m_filterInput = nullptr;
    geode::TextInput* m_onlineInput = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_onlineStatus = nullptr;
    std::string m_filter;
    std::string m_onlineQuery;
    std::vector<openverse::ResultItem> m_onlineItems;
    bool m_onlineBusy = false;

    // Audio FX controls
    Slider* m_volumeSlider = nullptr;  cocos2d::CCLabelBMFont* m_volumeLabel = nullptr;
    Slider* m_speedSlider = nullptr;   cocos2d::CCLabelBMFont* m_speedLabel = nullptr;
    Slider* m_pitchSlider = nullptr;   cocos2d::CCLabelBMFont* m_pitchLabel = nullptr;
    Slider* m_panSlider = nullptr;     cocos2d::CCLabelBMFont* m_panLabel = nullptr;
    Slider* m_dspMixSlider = nullptr;  cocos2d::CCLabelBMFont* m_dspMixLabel = nullptr;
    CCMenuItemSpriteExtra* m_dspBtn = nullptr;
    CCMenuItemSpriteExtra* m_maxDurBtn = nullptr;

    // Rules controls
    CCMenuItemSpriteExtra* m_orderButton = nullptr;
    CCMenuItemToggler* m_avoidRepeatToggle = nullptr;
    CCMenuItemToggler* m_stopOnResetToggle = nullptr;
    CCMenuItemToggler* m_allowOverlapToggle = nullptr;
    CCMenuItemToggler* m_layerVanillaToggle = nullptr;
    CCMenuItemToggler* m_clutchEnabledToggle = nullptr;
    CCMenuItemToggler* m_clutchNewBestToggle = nullptr;
    Slider* m_clutchThreshSlider = nullptr;
    cocos2d::CCLabelBMFont* m_clutchThreshLabel = nullptr;
    cocos2d::CCLabelBMFont* m_clutchSoundNameLabel = nullptr;
};

} // namespace paimon::death_effects
