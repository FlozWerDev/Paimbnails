// Visible IDs on the profile page: the user id and account id under the
// username, which is where GD already leaves a gap before the stat row.

#include "../InfoModule.hpp"
#include "../services/IdBadge.hpp"
#include "../../../framework/HookConventions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/modify/ProfilePage.hpp>

using namespace geode::prelude;

namespace {

char const* const kBadgeID = "info-suite-profile-ids";

bool profileIdsEnabled() {
    return paimon::info::subEnabled("info-mod-ids", "info-ids-users", true);
}

} // namespace

class $modify(PaimonInfoSuiteProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "ProfilePage::loadPageFromUserInfo");
    }

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (!profileIdsEnabled() || !score) return;

        auto* layer = m_mainLayer;
        if (!layer) return;
        if (auto* old = layer->getChildByID(kBadgeID)) old->removeFromParent();

        // An account id of 0 means an unregistered (green) player: only the
        // user id exists, so do not print a misleading "acc 0".
        std::string text = score->m_accountID > 0
            ? fmt::format("user {}  -  acc {}", score->m_userID, score->m_accountID)
            : fmt::format("user {}  -  sin cuenta", score->m_userID);

        auto* badge = paimon::info::makeIdBadge(text, 0.38f);
        if (!badge) return;

        badge->setID(kBadgeID);
        badge->setAnchorPoint({0.5f, 1.f});

        if (m_usernameLabel) {
            auto size = m_usernameLabel->getScaledContentSize();
            badge->setPosition({
                m_usernameLabel->getPositionX(),
                m_usernameLabel->getPositionY() - size.height * 0.5f - 2.f,
            });
        } else {
            auto layerSize = layer->getContentSize();
            badge->setPosition({layerSize.width / 2.f, layerSize.height - 46.f});
        }

        layer->addChild(badge, 100);
    }
};
