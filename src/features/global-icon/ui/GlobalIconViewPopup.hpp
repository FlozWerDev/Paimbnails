#pragma once
#include <Geode/Geode.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <vector>

namespace paimon::globalicon {

// Popup opened from a profile's custom icon: shows every icon that player
// shares (cube, ship, ball, ...) and lets you download one or wear it.
class GlobalIconViewPopup : public geode::Popup {
protected:
    struct Cell {
        GlobalIconSlot slot;
        cocos2d::CCNode* container = nullptr;   // owned by the scene graph
        SimplePlayer* preview = nullptr;
        cocos2d::CCSprite* placeholder = nullptr;
    };

    int m_accountID = 0;
    std::string m_username;
    std::vector<Cell> m_cells;
    int m_selected = -1;
    bool m_busy = false;

    cocos2d::extension::CCScale9Sprite* m_selectionRing = nullptr;
    cocos2d::CCLabelBMFont* m_captionLabel = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;

    bool init(int accountID, std::string const& username, GlobalIconMeta const& meta);

    void buildGrid(cocos2d::CCMenu* menu, std::vector<GlobalIconSlot> const& slots);
    void loadPreviews();
    void refreshPreviews();
    void selectCell(int index);
    void updateCaption();

    void onDownload(cocos2d::CCObject*);
    void onDownloadUse(cocos2d::CCObject*);

    // Runs `action` once the selected slot is on disk and registered.
    void withSelectedIcon(geode::CopyableFunction<void(std::string const& iconName, IconType type)> action);

public:
    // `meta` carries every slot the player shares; slots are ordered by the
    // canonical gamemode order so the grid reads the same for everyone.
    static GlobalIconViewPopup* create(int accountID, std::string const& username, GlobalIconMeta const& meta);
};

} // namespace paimon::globalicon
