#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>

namespace paimon::icon_gradients {

using namespace geode::prelude;

class $modify(GradientProfilePage, ProfilePage) {
public:
    struct Fields {
        bool m_isShip = true;
        bool m_isSecondPlayer = false;

        SEL_MenuHandler m_originalCallback = nullptr;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("ProfilePage::getUserInfoFinished", "alphalaneous.fine_outline");
    }

    void onSwap(CCObject*);

    void updateGradient();

    void getUserInfoFinished(GJUserScore*);

    void toggleShip(CCObject*);
};

} // namespace paimon::icon_gradients
