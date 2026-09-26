#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJUserScore.hpp>

#include "../data/VersusRanks.hpp"
#include "../services/VersusClient.hpp"
#include "../services/VersusStore.hpp"
#include "../ui/VersusProfilePopup.hpp"
#include "../ui/VersusRankBadgeNode.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace paimon::versus;

namespace {

constexpr char const* kChipId = "paimon-versus-chip"_spr;

// mirrors the progression chip in this row: mini badge plus rank, so both
// ladders read as one line.
CCNode* buildVersusChip(RankInfo const& rank, float height) {
    auto* text = CCLabelBMFont::create(rankShortName(rank).c_str(), "bigFont.fnt");
    if (!text) return nullptr;
    text->setScale((height * 0.42f) / std::max(1.f, text->getContentSize().height));

    float const badgeSize = height * 0.94f;
    float const width = badgeSize + text->getScaledContentSize().width + height * 0.44f;

    auto* chip = CCNode::create();
    chip->setContentSize({width, height});
    chip->setAnchorPoint({0.5f, 0.5f});

    if (auto* badge = VersusRankBadgeNode::create(rank, badgeSize)) {
        badge->setShowPips(false);
        badge->setPosition({height * 0.10f + badgeSize / 2.f, height / 2.f});
        chip->addChild(badge, 1);
    }

    text->setAnchorPoint({0.f, 0.5f});
    text->setPosition({height * 0.16f + badgeSize, height / 2.f});
    text->setColor(rankColor(rank));
    chip->addChild(text, 2);

    return chip;
}

} // namespace

class $modify(PaimonVersusProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "ProfilePage::loadPageFromUserInfo");
    }

    struct Fields {
        ModeProfile m_classic;
        ModeProfile m_platformer;
        std::string m_username;
        int m_accountId = 0;
        bool m_own = false;
        bool m_loaded = false;
    };

    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);

        if (!paimon::modules::isEnabled("paimbnails.versus.menu")) return;
        if (!score) return;

        m_fields->m_username = score->m_userName;
        m_fields->m_accountId = score->m_accountID;
        m_fields->m_own = this->m_ownProfile;

        if (m_fields->m_own) {
            auto& store = VersusStore::get();
            m_fields->m_classic = store.profile(Mode::Classic);
            m_fields->m_platformer = store.profile(Mode::Platformer);
            m_fields->m_loaded = true;
            applyChip();
            return;
        }

        // another player's ladder comes from the server while the page is up,
        // so the chip appears when the answer lands.
        auto self = Ref<PaimonVersusProfilePage>(this);
        VersusClient::get().fetchProfile(m_fields->m_accountId,
            [self](bool ok, ModeProfile const& classic, ModeProfile const& platformer) {
                if (!ok || !self->isRunning()) return;
                self->m_fields->m_classic = classic;
                self->m_fields->m_platformer = platformer;
                self->m_fields->m_loaded = true;
                self->applyChip();
            });
    }

    void applyChip() {
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("username-menu"));
        if (!menu) return;

        while (auto* existing = this->getChildByIDRecursive(kChipId)) {
            existing->removeFromParent();
        }

        // no chip for a never-duelled stranger; an unranked chip on every
        // profile would be noise.
        auto const& best = m_fields->m_classic.wins + m_fields->m_classic.losses >=
                           m_fields->m_platformer.wins + m_fields->m_platformer.losses
            ? m_fields->m_classic : m_fields->m_platformer;
        if (best.wins + best.losses == 0 && !m_fields->m_own) return;

        auto const rank = rankFor(best.elo, best.placementsLeft, best.paimon);
        auto* chip = buildVersusChip(rank, 20.f);
        if (!chip) return;

        auto* btn = CCMenuItemSpriteExtra::create(
            chip, this, menu_selector(PaimonVersusProfilePage::onVersusChip));
        if (!btn) return;
        btn->setID(kChipId);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onVersusChip(CCObject*) {
        if (!m_fields->m_loaded) return;
        if (auto* popup = VersusProfilePopup::create(
                m_fields->m_accountId, m_fields->m_username,
                m_fields->m_classic, m_fields->m_platformer, m_fields->m_own)) {
            popup->show();
        }
    }
};
