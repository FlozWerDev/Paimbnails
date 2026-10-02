#include "BadgeDetailPopup.hpp"
#include "BadgeIconNode.hpp"
#include "GDProgressBar.hpp"
#include "../data/ProgressionStats.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

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
    if (!PaimonPopup::init(300.f, 200.f)) return false;

    auto& loc = Localization::get();
    auto const size = m_mainLayer->getContentSize();
    float const cx = size.width / 2.f;
    bool const unlocked = isUnlocked(badge, ctx);
    auto const accent = rarityColor(badge.rarity);

    this->setTitle(badge.name);
    if (m_title) m_title->setColor(accent);

    if (auto* icon = BadgeIconNode::create(badge, ctx, 76.f)) {
        icon->setPosition({cx, size.height - 92.f});
        icon->playIntro(0.05f);
        m_mainLayer->addChild(icon);
    }

    auto* rarity = CCLabelBMFont::create(
        fmt::format("{}  -  {}", rarityLabel(badge.rarity), categoryLabel(badge.category)).c_str(),
        "chatFont.fnt"
    );
    rarity->setScale(0.44f);
    rarity->setColor(accent);
    rarity->setPosition({cx, size.height - 138.f});
    m_mainLayer->addChild(rarity);

    auto* requirement = CCLabelBMFont::create(badgeRequirement(badge).c_str(), "bigFont.fnt");
    requirement->limitLabelWidth(size.width - 46.f, 0.42f, 0.24f);
    requirement->setPosition({cx, size.height - 160.f});
    m_mainLayer->addChild(requirement);

    float const barW = size.width - 70.f;
    float const barY = 40.f;

    if (auto* bar = GDProgressBar::create(barW, 16.f)) {
        bar->setFillColor(accent);
        bar->animateTo(badgeProgress(badge, ctx), 0.12f, 0.45f);
        bar->setText(badgeProgressText(badge, ctx), 0.42f, {240, 245, 255});
        bar->setPosition({cx, barY});
        m_mainLayer->addChild(bar);
    }

    auto* state = CCLabelBMFont::create(
        loc.getString(unlocked ? "progression.badge.unlocked" : "progression.badge.locked").c_str(),
        "goldFont.fnt"
    );
    state->setScale(0.4f);
    state->setPosition({cx, barY + 28.f});
    if (!unlocked) state->setColor({150, 155, 170});
    m_mainLayer->addChild(state);

    paimon::markDynamicPopup(this);
    return true;
}

} // namespace paimon::progression
