#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../services/WallpaperEngineManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// browser for Wallpaper Engine projects and imported media. The list is
// virtualised: only the rows on screen exist and their previews decode on
// demand, so libraries with hundreds of workshop items open instantly.
class WallpaperPickerPopup : public PaimonPopup {
protected:
    struct Row {
        CCMenuItemSpriteExtra* button = nullptr;
        cocos2d::CCNode* card = nullptr;
        geode::NineSlice* background = nullptr;
        geode::NineSlice* highlight = nullptr;
        cocos2d::CCSprite* placeholder = nullptr;
        cocos2d::CCSprite* thumb = nullptr;
        cocos2d::CCLabelBMFont* title = nullptr;
        cocos2d::CCLabelBMFont* author = nullptr;
        cocos2d::CCLabelBMFont* badge = nullptr;
        cocos2d::CCLabelBMFont* status = nullptr;
        int index = -1;
        std::filesystem::path thumbPath;
        bool thumbReady = false;
    };

    std::string m_targetLayerKey;
    geode::CopyableFunction<void(paimon::wallpaper::WallpaperItem const&)> m_onPick;

    std::vector<paimon::wallpaper::WallpaperItem> m_allWallpapers;
    std::vector<size_t> m_filtered; // indices into m_allWallpapers
    paimon::wallpaper::WallpaperType m_currentFilter = paimon::wallpaper::WallpaperType::All;
    std::string m_appliedPath;
    bool m_webAvailable = false;

    int m_selected = -1; // index into m_filtered
    bool m_scanning = false;
    int m_scanGeneration = 0;
    std::string m_pendingSelectPath;

    cocos2d::CCMenu* m_tabMenu = nullptr;
    std::vector<std::pair<paimon::wallpaper::WallpaperType, CCMenuItemSpriteExtra*>> m_tabs;

    cocos2d::CCNode* m_listPanel = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_rowMenu = nullptr;
    std::vector<Row> m_rows;
    float m_rowWidth = 0.f;
    float m_contentHeight = 0.f;
    float m_lastScrollY = 1e9f;
    int m_firstBound = -1;
    uint64_t m_thumbRevision = 0;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    cocos2d::CCNode* m_detailPanel = nullptr;
    cocos2d::CCLabelBMFont* m_detailTitle = nullptr;
    cocos2d::CCLabelBMFont* m_detailAuthor = nullptr;
    cocos2d::CCLabelBMFont* m_detailInfo = nullptr;
    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCSprite* m_previewPlaceholder = nullptr;
    cocos2d::CCLabelBMFont* m_previewBadge = nullptr;
    std::filesystem::path m_previewPath;
    bool m_previewReady = false;
    cocos2d::CCSize m_previewSize;

    cocos2d::CCLabelBMFont* m_optionLabel = nullptr;
    CCMenuItemToggler* m_audioToggle = nullptr;
    CCMenuItemToggler* m_parallaxToggle = nullptr;
    cocos2d::CCLabelBMFont* m_fitLabel = nullptr;
    CCMenuItemSpriteExtra* m_fitButton = nullptr;
    CCMenuItemSpriteExtra* m_applyBtn = nullptr;
    CCMenuItemSpriteExtra* m_workshopBtn = nullptr;
    float m_detailWidth = 0.f;

    bool init(std::string const& layerKey, geode::CopyableFunction<void(paimon::wallpaper::WallpaperItem const&)> onPick);

    void buildTabs();
    void buildList(float width, float height);
    void buildDetail(float width, float height);
    void startScan(bool bypassCache);
    void onScanComplete(std::vector<paimon::wallpaper::WallpaperItem> items);
    void applyFilter(bool keepSelection);
    void updateStatusLabel();
    void layoutRows(bool force);
    void bindRow(Row& row, int index);
    void refreshRowThumb(Row& row);
    void refreshRowState(Row& row);
    void scrollToIndex(int index);
    void tick(float);
    void select(int index);
    void refreshPreview();
    void refreshOptions(paimon::wallpaper::WallpaperItem const& item);
    void onRowPressed(int index);
    void onClose(cocos2d::CCObject* sender) override;

    paimon::wallpaper::WallpaperItem const* selectedItem() const;
    static std::string playbackKey(paimon::wallpaper::WallpaperItem const& item);
    static std::string normalizedKey(std::string const& path);

    void onFilterTab(paimon::wallpaper::WallpaperType type);
    void onApplyCurrent();
    void onOpenWorkshop();
    void onImportFolder();
    void onOpenFolder();
    void onRefresh();
    void onToggleAudio(bool on);
    void onToggleParallax(bool on);
    void onCycleFitMode();

public:
    static WallpaperPickerPopup* create(
        std::string const& layerKey,
        geode::CopyableFunction<void(paimon::wallpaper::WallpaperItem const&)> onPick = nullptr
    );
};
