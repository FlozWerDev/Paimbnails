#pragma once

#include "../data/QuickHubCategories.hpp"

#include <Geode/Geode.hpp>

#include <functional>

namespace paimon::quickhub {

// Edita *target en vivo y avisa con onChanged; el padre conserva la propiedad.
class QuickButtonImagePopup : public geode::Popup {
public:
    static QuickButtonImagePopup* create(
        CustomQuickButton* target, std::function<void()> onChanged);

protected:
    bool init() override;

private:
    CustomQuickButton* m_target = nullptr;
    std::function<void()> m_onChanged;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNode* m_thumb = nullptr;
    cocos2d::CCLabelBMFont* m_scaleValue = nullptr;
    cocos2d::CCLabelBMFont* m_rotValue = nullptr;

    void changed();
    void refresh();
    void onChooseFile();
    void importImage(std::filesystem::path const& src);
};

} // namespace paimon::quickhub
