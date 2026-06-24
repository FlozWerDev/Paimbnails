#pragma once

#include "../engine/UiSpriteCatalog.hpp"
#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct SpritePreviewResult;

class SpriteBrowserPopup : public geode::Popup {
public:
    static SpriteBrowserPopup* create(std::string slotId,
                                      std::function<void()> onClosed);

protected:
    bool init(std::string slotId, std::function<void()> onClosed);
    void onClose(cocos2d::CCObject*) override;

    struct Entry {
        std::string frameName;
        int         sheetIndex = 0;   // índice en m_project.sheets
        SpriteKind  kind = SpriteKind::Other;
    };
    void buildEntries();

    void applyFilter();

    // Redibuja la página actual del grid.
    void rebuildGrid();

    void refreshFilterButtons();
    void openEditor(Entry const& entry);
    void requestTintedThumbnails(std::vector<Entry> entries, int generation);
    void applyTintedThumbnail(int slot, int generation,
                              std::shared_ptr<SpritePreviewResult> result);

private:
    std::string           m_slotId;
    std::function<void()> m_onClosed;
    TextureProject        m_project;

    std::vector<Entry> m_all;
    std::vector<int>   m_filtered;
    int         m_filterMode = 0;    // 0=Buttons 1=All UI 2=Edited
    int         m_page = 0;
    std::string m_search;

    cocos2d::CCNode*        m_gridHost = nullptr;
    cocos2d::CCMenu*        m_gridMenu = nullptr;
    cocos2d::CCLabelBMFont* m_pageLbl  = nullptr;
    cocos2d::CCLabelBMFont* m_countLbl = nullptr;
    CCMenuItemSpriteExtra*  m_filterButtonsBtn = nullptr;
    CCMenuItemSpriteExtra*  m_filterAllUiBtn   = nullptr;
    CCMenuItemSpriteExtra*  m_filterEditedBtn  = nullptr;
    std::shared_ptr<std::atomic<int>> m_renderGeneration =
        std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<bool>> m_closed =
        std::make_shared<std::atomic<bool>>(false);
};

}  // namespace paimon::texture_studio
