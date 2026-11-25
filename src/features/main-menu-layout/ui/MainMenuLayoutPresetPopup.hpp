#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <string>

namespace paimon::menu_layout {

class MainMenuLayoutPresetPopup : public PaimonPopup {
public:
    enum class Mode {
        Save,
        Load,
    };

    using SelectCallback = geode::CopyableFunction<void(int slotIndex)>;

    static MainMenuLayoutPresetPopup* create(
        Mode mode,
        SelectCallback onSelect,
        std::string const& sceneTag = "");

protected:
    bool init(Mode mode, SelectCallback onSelect, std::string const& sceneTag);

private:
    struct SlotCell {
        cocos2d::CCNode* card = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> hitButton = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> renameButton = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> deleteButton = nullptr;
        geode::Ref<geode::NineSlice> border = nullptr;
        geode::Ref<geode::NineSlice> fill = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> indexLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> titleLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> infoLabel = nullptr;
        geode::Ref<cocos2d::CCLabelBMFont> sceneLabel = nullptr;
    };

    void refresh();
    void updateSlotCell(int index);
    int usedPageCount() const;
    int totalPageCount() const;
    bool isPageFull(int page) const;
    bool canGoNext() const;
    int slotIndexForCell(int index) const;

    void onSlot(cocos2d::CCObject* sender);
    void onRenameSlot(cocos2d::CCObject* sender);
    void onDeleteSlot(cocos2d::CCObject* sender);
    void onPrev(cocos2d::CCObject* sender);
    void onNext(cocos2d::CCObject* sender);

    Mode m_mode = Mode::Load;
    int m_page = 0;
    std::string m_sceneTag;
    SelectCallback m_onSelect;

    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevButton = nullptr;
    CCMenuItemSpriteExtra* m_nextButton = nullptr;
    std::array<SlotCell, 6> m_slots;
};

} // namespace paimon::menu_layout
