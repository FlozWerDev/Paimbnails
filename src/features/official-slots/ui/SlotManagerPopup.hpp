#pragma once

// Mutations rebuild the list and repaint the LevelSelect pages; appended
// slots open for play here, having no vanilla page.

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
    void buildFooter();
    void rebuild();

    cocos2d::CCNode* buildSlotRow(Slot const& slot, float width);
    cocos2d::CCNode* buildHiddenRow(int officialId, float width);
    cocos2d::CCNode* buildSectionLabel(char const* key, float width);

    void onAdd(cocos2d::CCObject*);
    void onEditSlot(std::string const& slotId);
    void onTestSlot(Slot slot);
    void onDeleteSlot(Slot slot);
    void onMoveSlot(std::string const& slotId, int delta);
    void onReorder();
    void onToggleSlot(std::string const& slotId);
    void onRestoreOfficial(int officialId);

    void mutated();

    std::function<void()> m_onChanged;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::officialslots::ui
