#pragma once
//
// TextureStudioLayer.hpp - Top-level entry popup for the texture studio.
// Shows the slot grid, hosts the "New Pack" flow, and acts as the
// landing screen the MenuLayer button opens.
//
// Layout (matches the mockup in PLAN_TEXTURE_STUDIO.md):
//
//   ┌──────────────────────────────────────────────────┐
//   │ ◀  Texture Studio              [+ New Pack]      │
//   ├──────────────────────────────────────────────────┤
//   │                                                  │
//   │  [SlotsGridView with cards + "New Pack" tile]    │
//   │                                                  │
//   ├──────────────────────────────────────────────────┤
//   │ Active: "Mi Pack"   [Folder]  [Help]             │
//   └──────────────────────────────────────────────────┘
//

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace paimon::texture_studio {

class SlotsGridView;

class TextureStudioLayer : public geode::Popup {
public:
    static TextureStudioLayer* create();

protected:
    bool init();

    void onNewPack(cocos2d::CCObject*);
    void onApplySlot(std::string const& slotId);
    void onEditSlot(std::string const& slotId);
    void onDeleteSlot(std::string const& slotId);
    void onOpenFolder(cocos2d::CCObject*);

    void refreshFooter();

private:
    SlotsGridView*           m_grid       = nullptr;
    cocos2d::CCLabelBMFont*  m_activeLbl  = nullptr;
};

}  // namespace paimon::texture_studio
