#include "VersusLeaderboardLayer.hpp"
#include "VersusRankBadgeNode.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusRanks.hpp"
#include "../services/VersusStore.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kRowH = 40.f;
constexpr int kModeTag = 7000;
constexpr int kScopeTag = 7100;

char const* const kScopes[] = {"global", "friends", "season"};
char const* const kScopeKeys[] = {"versus.board.global", "versus.board.friends", "versus.board.season"};

} // namespace

VersusLeaderboardLayer* VersusLeaderboardLayer::create(Mode mode) {
    auto ret = new VersusLeaderboardLayer();
    if (ret && ret->init(mode)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* VersusLeaderboardLayer::scene(Mode mode) {
    auto* scene = CCScene::create();
    scene->addChild(VersusLeaderboardLayer::create(mode));
    return scene;
}

bool VersusLeaderboardLayer::init(Mode mode) {
    if (!CCLayer::init()) return false;

    m_mode = mode;
    this->setKeypadEnabled(true);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 20);

    buildChrome();
    buildTabs();
    load();
    return true;
}

void VersusLeaderboardLayer::buildChrome() {
    auto const winSize = CCDirector::get()->getWinSize();

    if (!LayerBackgroundManager::get().applyBackground(this, "versus_board")) {
        auto* bg = createLayerBG();
        bg->setZOrder(-10);
        this->addChild(bg);
        addSideArt(this, SideArt::All);
    }

    auto* title = CCLabelBMFont::create(
        Localization::get().getString("versus.board.title").c_str(), "bigFont.fnt");
    title->setScale(0.7f);
    title->setPosition({winSize.width / 2.f, winSize.height - 22.f});
    this->addChild(title, 10);

    auto* backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this, menu_selector(VersusLeaderboardLayer::onBack));
    backBtn->setPosition(25.f, winSize.height - 25.f);
    m_menu->addChild(backBtn);

    float const listW = std::min(400.f, winSize.width - 50.f);
    float const listH = winSize.height - 118.f;

    auto* frame = CCNode::create();
    frame->setContentSize({listW, listH});
    frame->setAnchorPoint({0.5f, 0.5f});
    frame->setPosition({winSize.width / 2.f, listH / 2.f + 18.f});
    this->addChild(frame, 1);

    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
        panel->setContentSize({listW, listH});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setColor({0, 0, 0});
        panel->setOpacity(130);
        frame->addChild(panel, 0);
    }

    m_scroll = ScrollLayer::create({listW, listH});
    frame->addChild(m_scroll, 1);

    auto* borders = ListBorders::create();
    borders->setContentSize({listW, listH});
    borders->setPosition({listW / 2.f, listH / 2.f});
    frame->addChild(borders, 5);

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setScale(0.5f);
    m_status->setOpacity(170);
    m_status->setPosition({winSize.width / 2.f, listH / 2.f + 18.f});
    this->addChild(m_status, 12);
}

void VersusLeaderboardLayer::buildTabs() {
    auto const winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    float const rowY = winSize.height - 56.f;

    char const* modeLabels[] = {"Classic", "Platformer"};
    for (int i = 0; i < 2; i++) {
        auto* btn = ui::makeTab(modeLabels[i], 72.f, this,
                                menu_selector(VersusLeaderboardLayer::onMode));
        btn->setTag(kModeTag + i);
        btn->setPosition({winSize.width / 2.f - 130.f + i * 76.f, rowY});
        m_menu->addChild(btn);
        m_modeButtons.push_back(btn);
    }

    // the two rows of tabs answer different questions, and side by side with no
    // gap they read as one row of five that happens to have two lit.
    auto* split = CCLayerColor::create(ccColor4B{255, 255, 255, 60}, 1.f, 22.f);
    split->setPosition({winSize.width / 2.f - 6.f, rowY - 11.f});
    this->addChild(split, 10);

    for (int i = 0; i < 3; i++) {
        auto* btn = ui::makeTab(loc.getString(kScopeKeys[i]), 66.f, this,
                                menu_selector(VersusLeaderboardLayer::onScope));
        btn->setTag(kScopeTag + i);
        btn->setPosition({winSize.width / 2.f + 40.f + i * 70.f, rowY});
        m_menu->addChild(btn);
        m_scopeButtons.push_back(btn);
    }
}

void VersusLeaderboardLayer::load() {
    // switching tabs mid-request has to win: the answer to the old one is
    // dropped rather than painted under the new tab.
    uint32_t const request = ++m_request;
    m_rows.clear();
    m_scroll->m_contentLayer->removeAllChildren();
    setStatus(Localization::get().getString("versus.board.loading"));

    for (size_t i = 0; i < m_modeButtons.size(); i++) {
        ui::styleTab(m_modeButtons[i], static_cast<int>(i) == static_cast<int>(m_mode));
    }
    for (size_t i = 0; i < m_scopeButtons.size(); i++) {
        ui::styleTab(m_scopeButtons[i], m_scope == kScopes[i]);
    }

    auto self = Ref<VersusLeaderboardLayer>(this);
    VersusClient::get().fetchLeaderboard(m_mode, m_scope,
        [self, request](bool ok, std::vector<LeaderboardRow> const& rows) {
            if (!self->isRunning() || request != self->m_request) return;

            if (!ok) {
                self->setStatus(Localization::get().getString("versus.board.failed"));
                return;
            }
            self->m_rows = rows;
            self->buildRows();
        });
}

void VersusLeaderboardLayer::buildRows() {
    if (m_rows.empty()) {
        setStatus(Localization::get().getString("versus.board.empty"));
        return;
    }
    setStatus("");

    float const listW = m_scroll->getContentSize().width;
    float const height = std::max(m_scroll->getContentSize().height, m_rows.size() * kRowH);
    m_scroll->m_contentLayer->setContentSize({listW, height});

    int const ownId = GJAccountManager::sharedState() ? GJAccountManager::sharedState()->m_accountID : 0;

    for (size_t i = 0; i < m_rows.size(); i++) {
        auto const& row = m_rows[i];
        float const y = height - kRowH * (i + 0.5f);
        bool const isSelf = row.accountId == ownId;

        auto* strip = CCLayerColor::create(
            isSelf ? ccColor4B{60, 90, 140, 150}
                   : (i % 2 ? ccColor4B{0, 0, 0, 60} : ccColor4B{0, 0, 0, 95}),
            listW, kRowH - 2.f);
        strip->setPosition({0.f, y - (kRowH - 2.f) / 2.f});
        m_scroll->m_contentLayer->addChild(strip, 0);

        auto* place = CCLabelBMFont::create(fmt::format("{}", row.rank).c_str(), "goldFont.fnt");
        place->setScale(0.5f);
        place->setAnchorPoint({0.5f, 0.5f});
        place->setPosition({24.f, y});
        m_scroll->m_contentLayer->addChild(place, 1);

        auto const rank = rankFor(row.elo);
        if (auto* badge = VersusRankBadgeNode::create(rank, 30.f)) {
            badge->setShowPips(false);
            badge->setPosition({58.f, y});
            m_scroll->m_contentLayer->addChild(badge, 1);
        }

        auto* name = CCLabelBMFont::create(row.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(std::min(0.5f, 150.f / std::max(1.f, name->getContentSize().width)));
        name->setPosition({82.f, y + 7.f});
        m_scroll->m_contentLayer->addChild(name, 1);

        auto* sub = CCLabelBMFont::create(
            fmt::format("{} - {}W {}L", rankName(rank), row.wins, row.losses).c_str(),
            "chatFont.fnt");
        sub->setAnchorPoint({0.f, 0.5f});
        sub->setScale(0.42f);
        sub->setOpacity(180);
        sub->setPosition({82.f, y - 9.f});
        m_scroll->m_contentLayer->addChild(sub, 1);

        auto* elo = CCLabelBMFont::create(fmt::format("{}", row.elo).c_str(), "bigFont.fnt");
        elo->setAnchorPoint({1.f, 0.5f});
        elo->setScale(0.5f);
        elo->setColor(rankColor(rank));
        elo->setPosition({listW - 12.f, y});
        m_scroll->m_contentLayer->addChild(elo, 1);
    }

    m_scroll->moveToTop();
}

void VersusLeaderboardLayer::setStatus(std::string const& text) {
    m_status->setString(text.c_str());
    m_status->setVisible(!text.empty());
}

void VersusLeaderboardLayer::onMode(CCObject* sender) {
    auto const mode = static_cast<Mode>(sender->getTag() - kModeTag);
    if (mode == m_mode) return;
    m_mode = mode;
    load();
}

void VersusLeaderboardLayer::onScope(CCObject* sender) {
    auto const scope = kScopes[std::clamp(sender->getTag() - kScopeTag, 0, 2)];
    if (m_scope == scope) return;
    m_scope = scope;
    load();
}

void VersusLeaderboardLayer::onBack(CCObject*) {
    keyBackClicked();
}

void VersusLeaderboardLayer::keyBackClicked() {
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

} // namespace paimon::versus
