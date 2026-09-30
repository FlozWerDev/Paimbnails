#pragma once
// icon maker gallery: card grid with true thumbnails, search and sort. full scene, like texture studio.

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker {

class IconGalleryLayer : public cocos2d::CCLayer {
public:
    static IconGalleryLayer* create();
    static cocos2d::CCScene* scene();
    static void open();

protected:
    enum class Sort { Recent = 0, Name = 1, Created = 2 };

    bool init() override;
    void onEnter() override;
    void keyBackClicked() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

    void buildBackground();
    void buildHeader();
    void rebuildGrid();
    cocos2d::CCNode* buildCard(std::string const& id, float width);

    void onBack();
    void onNewIcon();
    void onImportIcon();
    void onEditIcon(std::string const& slotId);
    void onIconMenu(std::string const& slotId);
    void onUseIcon(std::string const& slotId);
    void onShareIcon(std::string const& slotId);
    void onDuplicateIcon(std::string const& slotId);
    void onRenameIcon(std::string const& slotId);
    void onDeleteIcon(std::string const& slotId);
    void onFilterMenu();
    void onToggleFavorite(std::string const& slotId);

    void setStatus(std::string const& text);

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_scrollHost = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    std::string m_query;
    Sort m_sort = Sort::Recent;
    // -1 = all gamemodes; else index into supportedtypes().
    int m_typeFilter = -1;
    bool m_onlyFavorites = false;
    cocos2d::CCLabelBMFont* m_filterLabel = nullptr;
    cocos2d::CCSprite* m_favGlyph = nullptr;
    bool m_enteredOnce = false;
    bool m_busy = false;

    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
};

}  // namespace paimon::icon_maker
