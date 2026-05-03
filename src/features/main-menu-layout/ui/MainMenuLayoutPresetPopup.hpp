#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/function.hpp>

#include <array>

namespace paimon::menu_layout {

class MainMenuLayoutPresetPopup : public geode::Popup {
public:
    enum class Mode {
        Save,
        Load,
    };

    using SelectCallback = geode::CopyableFunction<void(int slotIndex)>;

    static MainMenuLayoutPresetPopup* create(Mode mode, SelectCallback onSelect);

protected:
    bool init(Mode mode, SelectCallback onSelect);

private:
    struct SlotCell {
        geode::Ref<CCMenuItemSpriteExtra> button = nullptr;
        geode::Ref<cocos2d::CCLayerColor> border = nullptr;
        geode::Ref<cocos2d::CCLayerColor> fill = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> titleLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> infoLabel = nullptr;
    };

    void refresh();
    void updateSlotCell(int index);
    int usedPageCount() const;
    int totalPageCount() const;
    bool isPageFull(int page) const;
    bool canGoNext() const;
    int slotIndexForCell(int index) const;

    void onSlot(cocos2d::CCObject* sender);
    void onPrev(cocos2d::CCObject* sender);
    void onNext(cocos2d::CCObject* sender);

    Mode m_mode = Mode::Load;
    int m_page = 0;
    SelectCallback m_onSelect;

    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevButton = nullptr;
    CCMenuItemSpriteExtra* m_nextButton = nullptr;
    std::array<SlotCell, 6> m_slots;
};

} // namespace paimon::menu_layout
