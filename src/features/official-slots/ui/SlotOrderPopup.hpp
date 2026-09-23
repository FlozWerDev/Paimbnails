#pragma once

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::officialslots::ui {

class SlotOrderPopup : public geode::Popup {
public:
    // onChanged runs after any move, so the manager can rebuild its own rows
    // next to the page repaint refreshOfficialList() triggers.
    static SlotOrderPopup* create(std::function<void()> onChanged = nullptr);

protected:
    bool init(std::function<void()> onChanged);

    void buildList();
    void buildControls();
    void rebuild();

    cocos2d::CCNode* buildRow(
        std::string const& key, int index, std::string const& title,
        bool dimmed, float width, bool selected);

    void onSelectSlot(std::string const& key);
    void onStep(int delta);
    void onJumpTop();
    void onJumpBottom();

    void refresh();
    void mutated();

    std::function<void()> m_onChanged;
    geode::ScrollLayer* m_scroll = nullptr;
    std::string m_selected;
    std::size_t m_rows = 0;
};

} // namespace paimon::officialslots::ui
