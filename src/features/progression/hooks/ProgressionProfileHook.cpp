#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJUserScore.hpp>

#include "../data/ProgressionStats.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../services/ProgressionService.hpp"
#include "../ui/GDProgressBar.hpp"
#include "../ui/ProgressionPopup.hpp"
#include "../ui/TierBadgeNode.hpp"
#include "../../versus/services/VersusClient.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace paimon::progression;

namespace {

// One chip per profile: mini tier badge plus the level, sized to sit in the
// username row next to the role badges.
CCNode* buildLevelChip(BadgeContext const& ctx, float height) {
    auto const& tier = tierForLevel(ctx.level);

    auto* text = CCLabelBMFont::create(fmt::format("Lv {}", ctx.level).c_str(), "bigFont.fnt");
    if (!text) return nullptr;
    float const textScale = (height * 0.46f) / std::max(1.f, text->getContentSize().height);
    text->setScale(textScale);

    float const badgeSize = height * 0.86f;
    float const width = badgeSize + text->getScaledContentSize().width + height * 0.46f;

    auto* chip = CCNode::create();
    chip->setContentSize({width, height});
    chip->setAnchorPoint({0.5f, 0.5f});

    if (auto* panel = GDProgressBar::makeCapsule()) {
        panel->setContentSize({width / (height / 20.f), 20.f});
        panel->setScale(height / 20.f);
        panel->setAnchorPoint({0.f, 0.5f});
        panel->setPosition({0.f, height / 2.f});
        panel->setColor({
            static_cast<GLubyte>(tier.base.r * 0.42f),
            static_cast<GLubyte>(tier.base.g * 0.42f),
            static_cast<GLubyte>(tier.base.b * 0.42f),
        });
        chip->addChild(panel, 0);
    }

    if (auto* badge = TierBadgeNode::create(ctx.level, badgeSize)) {
        badge->setPosition({height * 0.14f + badgeSize / 2.f, height / 2.f});
        chip->addChild(badge, 1);
    }

    text->setAnchorPoint({0.f, 0.5f});
    text->setPosition({height * 0.20f + badgeSize, height / 2.f});
    text->setColor(tier.accent);
    chip->addChild(text, 2);

    return chip;
}

} // namespace

class $modify(ProgressionProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        // After the redesign so the username menu is already in its final shape.
        paimon::hooks::veryLatePost(self, "ProfilePage::loadPageFromUserInfo");
    }

    struct Fields {
        BadgeContext m_ctx;
        std::string m_username;
    };

    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);

        auto& service = ProgressionService::get();
        if (!service.enabled()) return;

        if (this->m_ownProfile) {
            service.rememberOwnScore(score);
            m_fields->m_ctx = service.ownContext();
        } else {
            if (!score) return;
            m_fields->m_ctx = makeContext(statsFromScore(score));
        }
        if (score) m_fields->m_username = score->m_userName;

        applyLevelChip();
        if (!this->m_ownProfile && score) topUpVersusExp(score->m_accountID);
    }

    // Versus XP is unpublished, so the chip draws twice: short now, again
    // when the duel server answers.
    void topUpVersusExp(int accountId) {
        if (accountId <= 0) return;
        if (!paimon::modules::isEnabled("paimbnails.versus.menu")) return;

        auto self = Ref<ProgressionProfilePage>(this);
        paimon::versus::VersusClient::get().fetchProfile(accountId,
            [self](bool ok, paimon::versus::ModeProfile const& classic,
                   paimon::versus::ModeProfile const& platformer) {
                if (!ok || !self->isRunning()) return;
                if (classic.xpTotal + platformer.xpTotal == 0) return;

                auto stats = self->m_fields->m_ctx.stats;
                stats.versusExp = classic.xpTotal + platformer.xpTotal;
                stats.versusWins = classic.wins + platformer.wins;
                self->m_fields->m_ctx = makeContext(stats);
                self->applyLevelChip();
            });
    }

    void applyLevelChip() {
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("username-menu"));
        if (!menu) return;

        // The redesign relocates the chip into its own header, so a stale one
        // is not necessarily still under username-menu.
        std::string const id = "paimon-level-badge"_spr;
        while (auto* existing = this->getChildByIDRecursive(id)) {
            existing->removeFromParent();
        }

        auto* chip = buildLevelChip(m_fields->m_ctx, 20.f);
        if (!chip) return;

        auto* btn = CCMenuItemSpriteExtra::create(
            chip, this, menu_selector(ProgressionProfilePage::onLevelChip));
        if (!btn) return;
        btn->setID(id);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onLevelChip(CCObject*) {
        if (auto* popup = ProgressionPopup::create(m_fields->m_ctx, m_fields->m_username)) {
            popup->show();
        }
    }
};

// The very first snapshot has to be taken before any level is beaten, or the
// next completion would report a whole account's worth of XP as one gain.
class $modify(ProgressionMenuLayer, MenuLayer) {
    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        auto& service = ProgressionService::get();
        if (service.enabled() && !service.hasSnapshot()) {
            service.commitSnapshot();
        }
        return true;
    }
};
