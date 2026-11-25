#include "BadgeDetailPopup.hpp"
#include "BadgeIconNode.hpp"
#include "GDProgressBar.hpp"
#include "../data/ProgressionStats.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

BadgeDetailPopup* BadgeDetailPopup::create(BadgeDef const& badge, BadgeContext const& ctx) {
    auto ret = new BadgeDetailPopup();
    if (ret && ret->init(badge, ctx)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BadgeDetailPopup::init(BadgeDef const& badge, BadgeContext const& ctx) {
    if (!PaimonPopup::init(324.f, 238.f)) return false;

    auto& loc = Localization::get();
    auto const size = m_mainLayer->getContentSize();
    float const cx = size.width / 2.f;
    bool const unlocked = isUnlocked(badge, ctx);
    auto const accent = rarityColor(badge.rarity);

    this->addCorners(SideArtStyle::PopupGold, 0.65f);
    this->setTitle(badge.name, "goldFont.fnt", 0.68f, 18.f);
    if (m_title) m_title->setColor(accent);

    if (auto* div = paimon::ui::makeDivider(240.f, accent, 120)) {
        div->setPosition({cx, size.height - 34.f});
        m_mainLayer->addChild(div, 1);
    }

    if (auto* icon = BadgeIconNode::create(badge, ctx, 74.f)) {
        icon->setPosition({cx, size.height - 76.f});
        icon->playIntro(0.05f);
        m_mainLayer->addChild(icon, 2);
    }

    auto* card = paimon::ui::makeInset({288.f, 92.f}, 105);
    if (card) {
        card->setPosition({18.f, 44.f});
        m_mainLayer->addChild(card, 0);
    }

    auto* rarity = CCLabelBMFont::create(
        fmt::format("{}  -  {}", rarityLabel(badge.rarity), categoryLabel(badge.category)).c_str(),
        "bigFont.fnt"
    );
    rarity->setScale(0.29f);
    rarity->setColor(accent);
    rarity->setPosition({cx, 122.f});
    m_mainLayer->addChild(rarity, 1);

    auto* requirement = CCLabelBMFont::create(badgeRequirement(badge).c_str(), "bigFont.fnt");
    requirement->limitLabelWidth(268.f, 0.37f, 0.22f);
    requirement->setPosition({cx, 102.f});
    m_mainLayer->addChild(requirement, 1);

    auto* state = CCLabelBMFont::create(
        loc.getString(unlocked ? "progression.badge.unlocked" : "progression.badge.locked").c_str(),
        "goldFont.fnt"
    );
    state->setScale(0.38f);
    if (!unlocked) state->setColor({170, 178, 198});

    char const* statusFrame = unlocked ? "GJ_completesIcon_001.png" : "GJ_lock_001.png";
    if (auto* statusIcon = paimon::SpriteHelper::safeCreateWithFrameName(statusFrame)) {
        statusIcon->setScale(13.f / std::max(1.f, statusIcon->getContentSize().height));
        float const iconW = statusIcon->getScaledContentSize().width;
        float const textW = state->getScaledContentSize().width;
        float const gap = 5.f;
        float const totalW = iconW + gap + textW;
        statusIcon->setPosition({cx - totalW * 0.5f + iconW * 0.5f, 81.f});
        state->setPosition({cx - totalW * 0.5f + iconW + gap + textW * 0.5f, 81.f});
        m_mainLayer->addChild(statusIcon, 1);
    } else {
        state->setPosition({cx, 81.f});
    }
    m_mainLayer->addChild(state, 1);

    if (auto* bar = GDProgressBar::create(256.f, 15.f)) {
        bar->setFillColor(accent);
        bar->animateTo(badgeProgress(badge, ctx), 0.12f, 0.45f);
        bar->setText(badgeProgressText(badge, ctx), 0.30f, {255, 255, 255});
        bar->setPosition({cx, 58.f});
        m_mainLayer->addChild(bar, 1);
    }

    auto* menu = paimon::ui::makeZeroMenu();
    m_mainLayer->addChild(menu, 2);
    if (auto* okBtn = paimon::ui::makeButton(
            "OK",
            [this]() { this->onClose(nullptr); },
            paimon::ui::Btn::Green, 76.f, 0.72f, "goldFont.fnt")) {
        okBtn->setPosition({cx, 22.f});
        menu->addChild(okBtn);
    }

    paimon::markDynamicPopup(this);
    return true;
}

} // namespace paimon::progression
