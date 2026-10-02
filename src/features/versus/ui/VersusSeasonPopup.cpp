#include "VersusSeasonPopup.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusRanks.hpp"
#include "../services/VersusClient.hpp"
#include "../services/VersusStore.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 360.f;
constexpr float kPopupH = 260.f;
constexpr float kPanelW = 332.f;

std::string mutatorLabel(std::string const& id) {
    auto const key = "versus.mutator." + id;
    auto text = Localization::get().getString(key);
    return text == key ? id : text;
}

} // namespace

VersusSeasonPopup* VersusSeasonPopup::create() {
    auto ret = new VersusSeasonPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusSeasonPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    auto const& season = VersusClient::get().season();
    this->setTitle(season.number > 0
        ? fmt::format("{} {}", Localization::get().getString("versus.season.title"), season.number)
        : Localization::get().getString("versus.season.title"));

    buildSeason();
    buildMutators();

    auto* reset = CCLabelBMFont::create(
        Localization::get().getString("versus.season.reset").c_str(), "chatFont.fnt",
        kPanelW, kCCTextAlignmentCenter);
    reset->setScale(0.4f);
    reset->setOpacity(170);
    reset->setPosition({kPopupW / 2.f, 14.f});
    m_mainLayer->addChild(reset, 2);
    return true;
}

void VersusSeasonPopup::buildSeason() {
    auto& loc = Localization::get();
    auto const& season = VersusClient::get().season();
    auto& store = VersusStore::get();

    // the best of the two ladders is the one worth showing as the season badge.
    auto const& classic = store.profile(Mode::Classic);
    auto const& platformer = store.profile(Mode::Platformer);
    auto const& best = classic.best >= platformer.best ? classic : platformer;
    auto const rank = rankFor(best.best);

    CCSize const size = {kPanelW, 92.f};
    auto* panel = ui::makePanel(size, loc.getString("versus.season.best"));
    panel->setPosition({kPopupW / 2.f, 172.f});
    m_mainLayer->addChild(panel, 2);

    auto const body = ui::panelBody(size);

    if (auto* badge = VersusRankBadgeNode::create(rank, 56.f)) {
        badge->setShowPips(false);
        badge->setDim(best.wins + best.losses == 0);
        badge->setPosition({44.f, body.getMidY()});
        panel->addChild(badge, 1);
    }

    auto* name = ui::makeText(rankName(rank), "goldFont.fnt", 0.5f, {162.f, body.getMidY() + 10.f});
    name->setAnchorPoint({0.f, 0.5f});
    name->setScale(std::min(0.5f, 130.f / std::max(1.f, name->getContentSize().width)));
    name->setColor(rankColor(rank));
    panel->addChild(name, 1);

    auto* elo = ui::makeText(fmt::format("{} Elo", best.best), "chatFont.fnt", 0.44f,
                             {162.f, body.getMidY() - 10.f});
    elo->setAnchorPoint({0.f, 0.5f});
    elo->setOpacity(190);
    panel->addChild(elo, 1);

    auto* left = ui::makeText(
        season.daysLeft > 0
            ? fmt::format(fmt::runtime(loc.getString("versus.season.ends")), season.daysLeft)
            : loc.getString("versus.board.loading"),
        "bigFont.fnt", 0.44f, {size.width - 20.f, body.getMidY()});
    left->setAnchorPoint({1.f, 0.5f});
    left->setScale(std::min(0.44f, 110.f / std::max(1.f, left->getContentSize().width)));
    left->setColor(ui::kAccent);
    panel->addChild(left, 1);
}

void VersusSeasonPopup::buildMutators() {
    auto& loc = Localization::get();
    auto const& mutators = VersusClient::get().season().mutators;

    CCSize const size = {kPanelW, 84.f};
    auto* panel = ui::makePanel(size, loc.getString("versus.mutators"));
    panel->setPosition({kPopupW / 2.f, 74.f});
    m_mainLayer->addChild(panel, 2);

    auto const body = ui::panelBody(size);

    if (mutators.empty()) {
        auto* none = ui::makeText(loc.getString("versus.mutator.none"), "chatFont.fnt", 0.44f,
                                  {size.width / 2.f, body.getMidY()});
        none->setOpacity(170);
        panel->addChild(none, 1);
        return;
    }

    float y = body.getMaxY() - 12.f;
    for (auto const& id : mutators) {
        auto* label = CCLabelBMFont::create(mutatorLabel(id).c_str(), "chatFont.fnt",
                                            size.width - 24.f, kCCTextAlignmentCenter);
        label->setScale(0.44f);
        label->setColor({255, 200, 130});
        label->setPosition({size.width / 2.f, y});
        panel->addChild(label, 1);
        y -= 20.f;
    }
}

} // namespace paimon::versus
