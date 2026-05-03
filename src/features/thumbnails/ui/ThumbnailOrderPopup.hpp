#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/function.hpp>

#include <string>
#include <vector>

#include "../../../managers/ThumbnailAPI.hpp"

class ThumbnailOrderPopup : public geode::Popup {
protected:
    struct CellWidgets {
        geode::Ref<CCMenuItemSpriteExtra> button = nullptr;
        geode::Ref<cocos2d::CCNode> container = nullptr;
        geode::Ref<cocos2d::CCClippingNode> previewClip = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> orderLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> typeLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> mainLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> placeholderLabel = nullptr;
        bool previewRequested = false;
    };

    int m_levelID = 0;
    int m_selectedIndex = 0;
    bool m_isSaving = false;

    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCMenu* m_gridMenu = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_selectedLabel = nullptr;
    CCMenuItemSpriteExtra* m_moveLeftBtn = nullptr;
    CCMenuItemSpriteExtra* m_moveRightBtn = nullptr;
    CCMenuItemSpriteExtra* m_saveBtn = nullptr;
    CCMenuItemSpriteExtra* m_cancelBtn = nullptr;

    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
    std::vector<CellWidgets> m_cells;
    std::vector<std::string> m_originalOrderIds;

    geode::CopyableFunction<void(std::vector<ThumbnailAPI::ThumbnailInfo> const&, std::string const&)> m_onSaved;

    bool init(int levelID, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbnails, std::string const& selectedId);

    void buildCells();
    void layoutCells(bool resetScrollPosition);
    void updateUiState();
    void updateCellVisual(int index);
    void requestPreview(int index);
    void applyImagePreview(std::string const& thumbnailKey, cocos2d::CCTexture2D* texture);
    void applyVideoPreview(std::string const& thumbnailKey, class VideoThumbnailSprite* videoSprite);

    int indexForThumbnail(std::string const& thumbnailKey) const;
    std::string keyForThumbnail(ThumbnailAPI::ThumbnailInfo const& thumbnail) const;
    std::string getSelectedThumbnailKey() const;
    std::vector<std::string> getOrderIds() const;
    bool hasUnsavedChanges() const;

    void onSelectThumbnail(cocos2d::CCObject* sender);
    void onMoveLeft(cocos2d::CCObject* sender);
    void onMoveRight(cocos2d::CCObject* sender);
    void onSave(cocos2d::CCObject* sender);
    void onCancel(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject* sender) override;

public:
    static ThumbnailOrderPopup* create(
        int levelID,
        std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbnails,
        std::string const& selectedId
    );

    void setOnSaved(geode::CopyableFunction<void(std::vector<ThumbnailAPI::ThumbnailInfo> const&, std::string const&)> cb) {
        m_onSaved = std::move(cb);
    }
};
