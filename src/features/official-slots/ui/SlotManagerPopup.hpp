#pragma once

// List panel for the cosmetic official slots.
//
// One row per stored slot (paint preview, name, source, small actions) plus a
// section with the hidden officials and their restore buttons. Every mutation
// (add, edit, delete, reorder, toggle, restore) rebuilds the list and calls
// refreshOfficialList() so the LevelSelect pages repaint themselves.
//
// Slots are opened for play from here too: appended slots have no vanilla page,
// so the manager is their only door into the game.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::officialslots::ui {

class SlotManagerPopup : public geode::Popup {
public:
    // onChanged runs after any mutation, so the opener can refresh its own
    // buttons next to the page repaint refreshOfficialList() triggers.
    static SlotManagerPopup* create(std::function<void()> onChanged = nullptr);

protected:
    bool init(std::function<void()> onChanged);

    void buildHeader();
    void buildList();
    // Drops every row and lays the list out again from the store.
    void rebuild();

    cocos2d::CCNode* buildSlotRow(Slot const& slot, float width);
    cocos2d::CCNode* buildHiddenRow(int officialId, float width);
    cocos2d::CCNode* buildSectionLabel(char const* key, float width);

    void onAdd(cocos2d::CCObject*);
    void onEditSlot(std::string const& slotId);
    void onTestSlot(Slot slot);
    void onDeleteSlot(Slot slot);
    void onMoveSlot(std::string const& slotId, int delta);
    void onToggleSlot(std::string const& slotId);
    void onRestoreOfficial(int officialId);

    // Persist a change, then refresh list + pages + opener.
    void mutated();

    std::function<void()> m_onChanged;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::officialslots::ui
