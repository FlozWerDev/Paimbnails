#pragma once

#include "../data/QuickHubCategories.hpp"
#include <Geode/Geode.hpp>

namespace paimon::quickhub {

class QuickButtonPopup : public geode::Popup {
public:
    static QuickButtonPopup* create(CustomQuickButton candidate);
    static bool isOpen();

protected:
    bool init() override;
    void onExit() override;

private:
    CustomQuickButton m_candidate;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_idInput = nullptr;
    cocos2d::CCNode* m_preview = nullptr;
    CCMenuItemSpriteExtra* m_circleButton = nullptr;
    CCMenuItemSpriteExtra* m_squareButton = nullptr;
    CCMenuItemSpriteExtra* m_iconButton = nullptr;

    void rebuildPreview();
    void updateShapeButtons();
    void setShape(RadialButtonShape shape);
    void setIcon(std::string frame);
    void onChangeIcon(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);

    static QuickButtonPopup* s_instance;
};

} // namespace paimon::quickhub
