#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "ButtonSkinPopup.hpp"

#include <Geode/Geode.hpp>

#include <optional>
#include <string>

namespace paimon::buttonskin {

// right-click chooser: add the button to the quick hub, customize it, or drop its style.
class ButtonActionPopup : public PaimonPopup {
public:
    // quickHubIdentity: the clicked button exactly as the quick hub always captured it.
    static ButtonActionPopup* create(SkinEditTarget target, std::string skinId,
                                     quickhub::CustomQuickButton quickHubIdentity);
    static bool isOpen();

protected:
    bool init() override;
    void onExit() override;

private:
    SkinEditTarget m_target;
    quickhub::CustomQuickButton m_quickHubIdentity;
    std::optional<ButtonSkin> m_skin;

    void onQuickHub();
    void onCustomize();
    void onRemove();

    static ButtonActionPopup* s_instance;
};

} // namespace paimon::buttonskin
