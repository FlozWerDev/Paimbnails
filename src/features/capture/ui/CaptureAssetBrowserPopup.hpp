#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <vector>
#include <unordered_set>
#include <string>
#include "CapturePreviewPopup.hpp"

class GameObject;

class CaptureAssetBrowserPopup : public geode::Popup {
public:
    static CaptureAssetBrowserPopup* create(CapturePreviewPopup* previewPopup);

    static void restoreAllAssets();

    struct AssetGroup {
        int objectID = 0;
        std::string categoryKey;
        int count = 0;
        bool visible = true;
        bool originalVisible = true;
        cocos2d::CCSpriteFrame* representativeFrame = nullptr; // for preview (retained, released in onExit)
    };

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

    // Object type groups indexed by objectID
    std::vector<AssetGroup> m_groups;

    // Category header indices for display
    struct CategoryHeader {
        std::string name;
        bool visible = true;
        std::vector<int> groupIndices; // indices into m_groups
    };
    std::vector<CategoryHeader> m_categories;

    // Toggler references for in-place visual refresh
    std::vector<CCMenuItemToggler*> m_groupTogglers;

    void scanViewportObjects();
    void buildList();
    void updateMiniPreview();

    void setGroupVisible(int groupIdx, bool visible);
    void setCategoryVisible(int catIdx, bool visible);

    void onToggleGroup(cocos2d::CCObject* sender);
    void onToggleCategory(cocos2d::CCObject* sender);
    void onDoneBtn(cocos2d::CCObject* sender);
    void onRestoreAllBtn(cocos2d::CCObject* sender);
    void onShowAllBtn(cocos2d::CCObject* sender);
    void onHideAllBtn(cocos2d::CCObject* sender);

    static std::string categoryForObjectID(int objectID);
};
