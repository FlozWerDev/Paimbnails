#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../data/ShopPreset.hpp"

namespace paimon::shop {
class ShopCreatePopup : public PaimonPopup {
public:
    static ShopCreatePopup* create(std::function<void()> onCreated);
    ~ShopCreatePopup() override;
protected:
    bool init(std::function<void()> onCreated);
private:
    geode::TextInput* m_name = nullptr;
    geode::TextInput* m_description = nullptr;
    cocos2d::CCNode* m_previewHolder = nullptr;
    cocos2d::CCLabelBMFont* m_summary = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_categories;
    Category m_category = Category::Layout;
    std::string m_preview;
    std::function<void()> m_onCreated;
    bool m_saved = false;
    void selectCategory(Category category);
    void choosePreview();
    void savePreset();
};
} // namespace paimon::shop
