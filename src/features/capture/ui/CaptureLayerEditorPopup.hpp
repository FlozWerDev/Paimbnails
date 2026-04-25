#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <vector>
#include <string>
#include "CapturePreviewPopup.hpp"

class CaptureLayerEditorPopup : public geode::Popup {
public:
    static CaptureLayerEditorPopup* create(CapturePreviewPopup* previewPopup);

    static void restoreAllLayers();

protected:
    bool init() override;
    void onClose(cocos2d::CCObject*) override;
    void keyBackClicked() override;
    void onExit() override;

private:
    geode::WeakRef<CapturePreviewPopup> m_previewPopup = nullptr;
    cocos2d::CCSprite* m_miniPreview = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_listRoot = nullptr;

    // Smart filter system (dropdown of top-level branches)
    int m_filterGroupIndex = -1;
    cocos2d::CCLabelBMFont* m_filterLabel = nullptr;
    cocos2d::CCNode* m_filterDropdown = nullptr;

    struct LayerEntry {
        cocos2d::CCNode* node = nullptr;
        std::string name;
        bool currentVisibility = true;
        bool originalVisibility = true;
        bool isGroup = false;
        int depth = 0;
        int parentIndex = -1;
        std::vector<int> childIndices;
        CCMenuItemToggler* toggler = nullptr;
        cocos2d::CCLabelBMFont* label = nullptr;
    };

    std::vector<LayerEntry> m_layers;

    // Instance-level original visibilities (was static, now per-popup)
    std::vector<std::pair<geode::WeakRef<cocos2d::CCNode>, bool>> m_originalVisibilities;

    void populateLayers();
    void buildList();
    void updateMiniPreview();
    void refreshRowVisuals(int idx);
    [[nodiscard]] bool isEntryVisible(int idx) const;
    [[nodiscard]] bool entryMatchesFilter(int idx) const;
    void setEntryVisible(int idx, bool visible, bool cascadeChildren);
    void onToggleLayer(cocos2d::CCObject* sender);
    void onFilterBtn(cocos2d::CCObject* sender);
    void onFilterSelect(cocos2d::CCObject* sender);
    void closeFilterDropdown();
    void onDoneBtn(cocos2d::CCObject* sender);
    void onRestoreAllBtn(cocos2d::CCObject* sender);
};
