#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../services/PetManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <vector>

class PetDialoguesPopup final : public PaimonPopup {
protected:
    PetDialogueCategory m_category = PetDialogueCategory::Idle;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_summaryLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;

    bool init(PetDialogueCategory initialCategory);
    void buildTabs();
    void selectCategory(PetDialogueCategory cat);
    void rebuildList();
    void openEntryEditor(int editIndex);

public:
    static PetDialoguesPopup* create(PetDialogueCategory initialCategory = PetDialogueCategory::Idle);
};

class PetAudioGesturesPopup final : public PaimonPopup {
protected:
    // 0 = Click pool, 1..6 = PetGesture (index - 1)
    int m_selectedSlot = 0;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_summaryLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;

    bool init(int initialSlot);
    void buildTabs();
    void selectSlot(int slot);
    void rebuildList();
    std::vector<std::string>& currentPool();
    std::string currentPoolKey() const;
    void openGdSfxPicker();

public:
    static PetAudioGesturesPopup* create(int initialSlot = 0);
};
