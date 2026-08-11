#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/CharacterColorPage.hpp>

namespace paimon::icon_gradients {

class $modify(GradientCharacterColorPage, CharacterColorPage) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("CharacterColorPage::onPlayerColor", "alphalaneous.fine_outline");
    }

    struct Fields {
        bool m_isShip = true;
    };

    void updateGradient();

    bool init();

    void toggleShip(CCObject*);

    void onPlayerColor(CCObject*);

    void onClose(CCObject*);

    void keyBackClicked();
};

} // namespace paimon::icon_gradients
