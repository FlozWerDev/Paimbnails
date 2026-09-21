#pragma once

#include "../data/QuickHubCategories.hpp"

#include <Geode/Geode.hpp>

#include <functional>

namespace paimon::quickhub {

// Editor vivo del SFX del boton; el padre conserva la propiedad del CustomQuickButton.
class QuickButtonSfxPopup : public geode::Popup {
public:
    static QuickButtonSfxPopup* create(
        CustomQuickButton* target, std::function<void()> onChanged);

protected:
    bool init() override;
    void onExit() override;

private:
    CustomQuickButton* m_target = nullptr;
    std::function<void()> m_onChanged;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNode* m_ctx = nullptr;
    cocos2d::CCMenu* m_dynMenu = nullptr;
    geode::TextInput* m_gameInput = nullptr;
    geode::TextInput* m_onlineInput = nullptr;
    cocos2d::CCLabelBMFont* m_fileLabel = nullptr;
    cocos2d::CCLabelBMFont* m_volValue = nullptr;
    cocos2d::CCLabelBMFont* m_speedValue = nullptr;
    cocos2d::CCLabelBMFont* m_startValue = nullptr;
    cocos2d::CCLabelBMFont* m_endValue = nullptr;
    cocos2d::CCLabelBMFont* m_fadeValue = nullptr;
    cocos2d::CCLabelBMFont* m_durLabel = nullptr;

    void changed();
    void refresh();
    void updateDuration();
    // Lee los TextInput visibles al candidato antes de reconstruir o cerrar.
    void syncInputs();
    void onChooseAudio();
    void importAudio(std::filesystem::path const& src);
};

} // namespace paimon::quickhub
