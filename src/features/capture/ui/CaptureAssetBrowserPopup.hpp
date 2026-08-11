#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <vector>
#include <string>
#include "CapturePreviewPopup.hpp"

class GameObject;

namespace paimon::capture { class MiniPreview; }

class CaptureAssetBrowserPopup : public geode::Popup {
public:
    static CaptureAssetBrowserPopup* create(CapturePreviewPopup* previewPopup);

    static void restoreAllAssets();
    static void discardTrackedAssets();

    struct AssetGroup {
        int objectID = 0;
        std::string categoryKey;
        int count = 0;
        bool visible = true;
        cocos2d::CCSpriteFrame* representativeFrame = nullptr; // retained; released in dtor
        std::vector<GameObject*> objects;                      // every instance of this ID
        CCMenuItemToggler* toggler = nullptr;                  // rebuilt with the list
        cocos2d::CCLabelBMFont* label = nullptr;
    };

    struct CategoryHeader {
        std::string name;
        bool collapsed = false;
        std::vector<int> groupIndices; // indices into m_groups
        CCMenuItemToggler* toggler = nullptr;
        cocos2d::CCLabelBMFont* label = nullptr;
    };

protected:
    bool init() override;
    void onClose(cocos2d::CCObject*) override;
    void keyBackClicked() override;
    void onExit() override;

private:
    ~CaptureAssetBrowserPopup() override;

    enum class TriState { Hidden, Partial, Visible };

    geode::WeakRef<CapturePreviewPopup> m_previewPopup = nullptr;
    geode::WeakRef<cocos2d::CCNode> m_scannedPlayLayer = nullptr;

    paimon::capture::MiniPreview* m_miniPreview = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_listRoot = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_collapseLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    std::vector<AssetGroup> m_groups;
    std::vector<CategoryHeader> m_categories;

    std::string m_searchQuery;
    bool m_allCollapsed = false;

    void scanObjects();
    void buildList();
    void refreshPreview();
    void updateStats();
    void updateRowVisuals(int groupIdx);
    void updateCategoryVisuals(int catIdx);

    [[nodiscard]] bool groupMatchesSearch(int groupIdx) const;
    [[nodiscard]] bool categoryHasMatches(int catIdx) const;
    [[nodiscard]] TriState categoryState(int catIdx) const;
    [[nodiscard]] bool playLayerStillValid() const;

    void snapshotGroup(int groupIdx);
    void setGroupVisible(int groupIdx, bool visible);
    void setCategoryVisible(int catIdx, bool visible);
    void setMatchingVisible(bool visible);
    void soloGroup(int groupIdx);
    void refreshGroupStatesFromScene();

    void onToggleGroup(cocos2d::CCObject* sender);
    void onToggleCategory(cocos2d::CCObject* sender);
    void onToggleCollapse(cocos2d::CCObject* sender);
    void onSoloGroup(cocos2d::CCObject* sender);
    void onCollapseAllBtn(cocos2d::CCObject* sender);
    void onClearSearchBtn(cocos2d::CCObject* sender);
    void onDoneBtn(cocos2d::CCObject* sender);
    void onRestoreAllBtn(cocos2d::CCObject* sender);
    void onShowAllBtn(cocos2d::CCObject* sender);
    void onHideAllBtn(cocos2d::CCObject* sender);

    void onSearchChanged(std::string const& text);

    static std::string categoryForObjectID(int objectID);
};
