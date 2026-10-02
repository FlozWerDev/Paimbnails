#include "../../../ui/PaimonUI.hpp"
#include "VersusHubLayer.hpp"
#include "VersusDeckPopup.hpp"
#include "VersusFriendlyPopup.hpp"
#include "VersusHistoryPopup.hpp"
#include "VersusLeaderboardLayer.hpp"
#include "VersusMatchPopup.hpp"
#include "VersusSeasonPopup.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusModes.hpp"
#include "../services/VersusClient.hpp"
#include "../services/VersusGlobed.hpp"
#include "../services/VersusSession.hpp"
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

constexpr int kModeTag = 4000;
constexpr int kFormatTag = 4100;

constexpr float kMargin = 12.f;
constexpr float kGap = 8.f;
constexpr float kRankW = 150.f;
constexpr float kMoreW = 112.f;
constexpr float kRowH = 44.f;

// what the format costs you in time or in tries, which is the one thing the
// win condition never says.
std::string limitText(FormatDef const& def) {
    auto& loc = Localization::get();
    if (def.attemptLimit > 0) {
        return fmt::format(fmt::runtime(loc.getString("versus.limit.attempts")), def.attemptLimit);
    }
    if (def.timeLimit > 0) {
        return fmt::format("{}:{:02d}", def.timeLimit / 60, def.timeLimit % 60);
    }
    return loc.getString("versus.limit.none");
}

CCNode* buildFormatRow(FormatDef const& def, float width, bool selected) {
    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});
    row->setAnchorPoint({0.5f, 0.5f});

    auto* bg = paimon::ui::makeInset({width, kRowH - 4.f}, selected ? 165 : 85);
    bg->setPosition({0.f, 2.f});
    row->addChild(bg, 0);

    if (selected) {
        auto* mark = CCLayerColor::create(ccColor4B{255, 226, 140, 235}, 3.f, kRowH - 12.f);
        mark->setPosition({4.f, 6.f});
        row->addChild(mark, 1);
    }

    float textX = 14.f;
    if (auto* glyph = paimon::SpriteHelper::safeCreate(formatSprite(def).c_str())) {
        glyph->setScale(26.f / std::max(1.f, glyph->getContentSize().width));
        glyph->setPosition({28.f, kRowH / 2.f});
        if (!selected) glyph->setColor({198, 204, 220});
        row->addChild(glyph, 1);
        textX = 46.f;
    }

    auto* limit = CCLabelBMFont::create(limitText(def).c_str(), "chatFont.fnt");
    limit->setAnchorPoint({1.f, 0.5f});
    limit->setScale(0.38f);
    limit->setOpacity(selected ? 210 : 150);
    limit->setPosition({width - 10.f, kRowH - 15.f});
    row->addChild(limit, 1);

    float const textW = width - textX - limit->getScaledContentSize().width - 18.f;

    auto* name = CCLabelBMFont::create(formatName(def).c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->setScale(std::min(0.44f, textW / std::max(1.f, name->getContentSize().width)));
    name->setColor(selected ? ui::kAccent : ccColor3B{235, 238, 246});
    name->setPosition({textX, kRowH - 15.f});
    row->addChild(name, 1);

    auto* rule = CCLabelBMFont::create(formatWinCondition(def).c_str(), "chatFont.fnt");
    rule->setAnchorPoint({0.f, 0.5f});
    rule->setScale(std::min(0.4f, (width - textX - 12.f) /
                                  std::max(1.f, rule->getContentSize().width)));
    rule->setOpacity(selected ? 220 : 165);
    rule->setPosition({textX, 15.f});
    row->addChild(rule, 1);

    return row;
}

} // namespace

VersusHubLayer* VersusHubLayer::create() {
    auto ret = new VersusHubLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* VersusHubLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(VersusHubLayer::create());
    return scene;
}

bool VersusHubLayer::init() {
    if (!CCLayer::init()) return false;

    m_mode = VersusStore::get().preferredMode();
    this->setKeypadEnabled(true);

    m_menu = CCMenu::create();
    m_menu->setPosition(0.f, 0.f);
    this->addChild(m_menu, 20);

    auto const winSize = CCDirector::get()->getWinSize();
    float const top = winSize.height - 46.f;
    float const bottom = 74.f;
    float const height = top - bottom;
    float const midW = std::max(170.f, winSize.width - 2.f * kMargin - kRankW - kMoreW - 2.f * kGap);

    buildChrome();
    buildRankPanel({kMargin, bottom, kRankW, height});
    buildFormatPanel({kMargin + kRankW + kGap, bottom, midW, height});
    buildShortcuts({winSize.width - kMargin - kMoreW, bottom, kMoreW, height});
    buildActions(48.f);

    refreshRank();
    refreshFormats();
    refreshPlayButton();
    return true;
}

void VersusHubLayer::buildChrome() {
    auto const winSize = CCDirector::get()->getWinSize();

    if (!LayerBackgroundManager::get().applyBackground(this, "versus")) {
        paimon::ui::decorateScene(this);
    }

    if (auto* logo = paimon::SpriteHelper::safeCreate("paim_vsLogo.png"_spr)) {
        logo->setScale(34.f / std::max(1.f, logo->getContentSize().height));
        logo->setPosition({winSize.width / 2.f, winSize.height - 24.f});
        logo->setColor(ui::kAccent);
        this->addChild(logo, 10);
    }

    auto* backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(VersusHubLayer::onBack)
    );
    backBtn->setPosition(25.f, winSize.height - 25.f);
    m_menu->addChild(backBtn);

    // the fast channel is a rule of the ladder, so it says so up here instead
    // of only turning up as an error the moment somebody presses play.
    m_globedLabel = ui::makeText("", "chatFont.fnt", 0.42f,
                                 {winSize.width - 14.f, winSize.height - 22.f});
    m_globedLabel->setAnchorPoint({1.f, 0.5f});
    this->addChild(m_globedLabel, 10);
    refreshGlobed();

    m_status = ui::makeText("", "chatFont.fnt", 0.46f, {winSize.width / 2.f, 12.f});
    m_status->setOpacity(190);
    this->addChild(m_status, 10);
}

void VersusHubLayer::buildRankPanel(CCRect const& area) {
    auto& loc = Localization::get();

    m_rankPanel = ui::makePanel(area.size, loc.getString("versus.hub.rank"));
    m_rankPanel->setPosition({area.getMidX(), area.getMidY()});
    this->addChild(m_rankPanel, 5);

    auto const body = ui::panelBody(area.size);

    // the two ladders never mix, so the toggle lives inside the panel it
    // rewrites rather than floating above it.
    char const* labels[] = {"Classic", "Platformer"};
    float const tabW = (body.size.width - 4.f) / 2.f;
    for (int i = 0; i < 2; i++) {
        auto* btn = ui::makeTab(labels[i], tabW, this, menu_selector(VersusHubLayer::onMode));
        btn->setTag(kModeTag + i);
        btn->setPosition({
            area.origin.x + body.origin.x + tabW / 2.f + i * (tabW + 4.f),
            area.origin.y + body.getMaxY() - 14.f,
        });
        m_menu->addChild(btn);
        m_modeButtons.push_back(btn);
    }

    float const centerX = area.size.width / 2.f;

    m_badge = VersusRankBadgeNode::create(VersusStore::get().rank(m_mode), 80.f);
    m_badge->setPosition({centerX, body.getMaxY() - 70.f});
    m_rankPanel->addChild(m_badge, 1);

    m_rankLabel = ui::makeText("", "goldFont.fnt", 0.56f,
                               {centerX, body.origin.y + 46.f});
    m_rankPanel->addChild(m_rankLabel, 1);

    m_eloLabel = ui::makeText("", "bigFont.fnt", 0.44f,
                              {centerX, body.origin.y + 26.f});
    m_rankPanel->addChild(m_eloLabel, 1);

    if (auto* rule = ui::makeDivider(body.size.width - 20.f)) {
        rule->setPosition({body.origin.x + 10.f, body.origin.y + 16.f});
        m_rankPanel->addChild(rule, 1);
    }

    m_recordLabel = ui::makeText("", "chatFont.fnt", 0.5f,
                                 {centerX, body.origin.y + 6.f});
    m_recordLabel->setOpacity(200);
    m_rankPanel->addChild(m_recordLabel, 1);
}

void VersusHubLayer::buildFormatPanel(CCRect const& area) {
    auto& loc = Localization::get();

    m_formatPanel = ui::makePanel(area.size, loc.getString("versus.formats"));
    m_formatPanel->setPosition({area.getMidX(), area.getMidY()});
    this->addChild(m_formatPanel, 5);

    auto const body = ui::panelBody(area.size);

    auto* hint = ui::makeText(loc.getString("versus.hub.pick-format"), "chatFont.fnt", 0.4f,
                              {area.size.width / 2.f, body.getMaxY() - 8.f});
    hint->setOpacity(160);
    m_formatPanel->addChild(hint, 1);

    m_formatList = ScrollLayer::create({body.size.width, body.size.height - 20.f});
    m_formatList->setPosition({body.origin.x, body.origin.y});
    m_formatPanel->addChild(m_formatList, 1);
}

void VersusHubLayer::buildShortcuts(CCRect const& area) {
    auto& loc = Localization::get();

    auto* panel = ui::makePanel(area.size, loc.getString("versus.hub.more"));
    panel->setPosition({area.getMidX(), area.getMidY()});
    this->addChild(panel, 5);

    auto const body = ui::panelBody(area.size);
    float const rowW = body.size.width - 6.f;

    struct Shortcut {
        char const* icon;
        char const* key;
        SEL_MenuHandler handler;
    };
    Shortcut const shortcuts[] = {
        {"GJ_rankIcon_001.png",      "versus.hub.board",   menu_selector(VersusHubLayer::onLeaderboard)},
        {"GJ_sTrendingIcon_001.png", "versus.hub.season",  menu_selector(VersusHubLayer::onSeason)},
        {"GJ_timeIcon_001.png",      "versus.hub.history", menu_selector(VersusHubLayer::onHistory)},
        {"paim_vsCardBack.png"_spr,  "versus.hub.deck",    menu_selector(VersusHubLayer::onDeck)},
    };

    float y = area.origin.y + body.getMaxY() - 22.f;
    for (auto const& shortcut : shortcuts) {
        auto* btn = ui::makeIconRow(shortcut.icon, loc.getString(shortcut.key), rowW,
                                    this, shortcut.handler);
        btn->setPosition({area.getMidX(), y});
        m_menu->addChild(btn);
        y -= 38.f;
    }
}

void VersusHubLayer::buildActions(float centerY) {
    auto const winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    float const rankedX = winSize.width / 2.f - 92.f;
    float const friendlyX = winSize.width / 2.f + 92.f;

    m_playButton = ui::makeAction(loc.getString("versus.play"), 150, "GJ_button_01.png", 0.6f,
                                  this, menu_selector(VersusHubLayer::onPlay));
    m_playButton->setPosition({rankedX, centerY});
    m_menu->addChild(m_playButton);

    auto* rankedNote = ui::makeText(loc.getString("versus.hub.ranked-note"), "chatFont.fnt", 0.38f,
                                    {rankedX, centerY - 24.f});
    rankedNote->setOpacity(160);
    this->addChild(rankedNote, 10);

    auto* friendly = ui::makeAction(loc.getString("versus.friendly"), 130, "GJ_button_05.png", 0.6f,
                                    this, menu_selector(VersusHubLayer::onFriendly));
    friendly->setPosition({friendlyX, centerY});
    m_menu->addChild(friendly);

    auto* friendlyNote = ui::makeText(loc.getString("versus.hub.friendly-note"), "chatFont.fnt",
                                      0.38f, {friendlyX, centerY - 24.f});
    friendlyNote->setOpacity(160);
    this->addChild(friendlyNote, 10);
}

void VersusHubLayer::refreshRank() {
    auto& store = VersusStore::get();
    auto const& profile = store.profile(m_mode);
    auto const rank = store.rank(m_mode);

    m_badge->setRank(rank);
    m_badge->setDim(profile.wins + profile.losses == 0);
    m_rankLabel->setString(rankName(rank).c_str());
    m_rankLabel->setColor(rankColor(rank));

    if (rank.placing()) {
        m_eloLabel->setString(fmt::format(
            fmt::runtime(Localization::get().getString("versus.placements-left")),
            rank.placementsLeft).c_str());
    } else {
        m_eloLabel->setString(fmt::format("{} Elo", profile.elo).c_str());
    }

    m_recordLabel->setString(fmt::format("{}W - {}L", profile.wins, profile.losses).c_str());

    for (size_t i = 0; i < m_modeButtons.size(); i++) {
        ui::styleTab(m_modeButtons[i], static_cast<int>(i) == static_cast<int>(m_mode));
    }
}

void VersusHubLayer::refreshFormats() {
    m_formatRows.clear();
    m_formatList->m_contentLayer->removeAllChildren();

    auto const formats = queueableFormats(m_mode);
    auto const selected = VersusStore::get().preferredFormat(m_mode);

    float const listW = m_formatList->getContentSize().width;
    float const listH = m_formatList->getContentSize().height;
    float const height = std::max(listH, formats.size() * kRowH);
    m_formatList->m_contentLayer->setContentSize({listW, height});

    auto* rowMenu = CCMenu::create();
    rowMenu->setPosition({0.f, 0.f});
    m_formatList->m_contentLayer->addChild(rowMenu, 1);

    for (size_t i = 0; i < formats.size(); i++) {
        auto const* def = formats[i];
        auto* face = buildFormatRow(*def, listW - 6.f, def->id == selected);
        auto* btn = CCMenuItemSpriteExtra::create(face, this,
                                                  menu_selector(VersusHubLayer::onFormat));
        btn->setTag(kFormatTag + static_cast<int>(def->id));
        btn->setPosition({listW / 2.f, height - kRowH * (i + 0.5f)});
        rowMenu->addChild(btn);
        m_formatRows.push_back(btn);
    }

    m_formatList->moveToTop();
}

void VersusHubLayer::refreshPlayButton() {
    if (!m_playButton) return;

    bool const queued = VersusSession::get().phase() == Phase::Queued;
    auto const key = queued ? "versus.cancel-search" : "versus.play";

    if (auto* face = typeinfo_cast<ButtonSprite*>(m_playButton->getNormalImage())) {
        face->setString(Localization::get().getString(key).c_str());
        face->updateBGImage(queued ? "GJ_button_06.png" : "GJ_button_01.png");
    }
}

void VersusHubLayer::refreshGlobed() {
    auto& loc = Localization::get();

    if (!gl::present()) {
        m_globedLabel->setString(loc.getString("versus.globed.missing").c_str());
        m_globedLabel->setColor({200, 160, 240});
    } else if (!gl::connected()) {
        m_globedLabel->setString(loc.getString("versus.globed.off").c_str());
        m_globedLabel->setColor(ui::kBad);
    } else {
        m_globedLabel->setString(loc.getString("versus.globed.on").c_str());
        m_globedLabel->setColor(ui::kGood);
    }
}

void VersusHubLayer::tickChrome(float) {
    refreshGlobed();
}

void VersusHubLayer::setStatus(std::string const& text, bool error) {
    m_status->setString(text.c_str());
    m_status->setColor(error ? ui::kBad : ccColor3B{255, 255, 255});
}

void VersusHubLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();

    auto self = Ref<VersusHubLayer>(this);
    VersusSession::get().addListener(this, [self]() {
        if (self->isRunning()) self->onSessionChanged();
    });
    VersusSession::get().beginWatch();

    this->schedule(schedule_selector(VersusHubLayer::tickChrome), 1.f);
    refreshGlobed();
    onSessionChanged();

    if (VersusSession::get().idle()) {
        auto const& def = formatAt(VersusStore::get().preferredFormat(m_mode));
        setStatus(formatName(def) + " - " + formatWinCondition(def));
    }

    // nothing happens without a token, and the invite that arrives while the
    // hub sits idle needs one as much as the queue does.
    if (!VersusClient::get().authenticated()) {
        authenticateThen([]() {});
    }
}

void VersusHubLayer::onExit() {
    VersusSession::get().removeListener(this);
    VersusSession::get().endWatch();
    CCLayer::onExit();
}

void VersusHubLayer::onMode(CCObject* sender) {
    auto const mode = static_cast<Mode>(sender->getTag() - kModeTag);
    if (mode == m_mode) return;

    m_mode = mode;
    VersusStore::get().setPreferredMode(mode);
    refreshRank();
    refreshFormats();

    auto const& def = formatAt(VersusStore::get().preferredFormat(m_mode));
    setStatus(formatName(def) + " - " + formatWinCondition(def));
}

void VersusHubLayer::onFormat(CCObject* sender) {
    auto const format = static_cast<Format>(sender->getTag() - kFormatTag);
    VersusStore::get().setPreferredFormat(m_mode, format);
    refreshFormats();

    auto const& def = formatAt(format);
    setStatus(formatName(def) + " - " + formatWinCondition(def));
}

void VersusHubLayer::authenticateThen(std::function<void()> next) {
    if (VersusClient::get().authenticated()) {
        next();
        return;
    }

    m_busy = true;
    setStatus(Localization::get().getString("versus.connecting"));

    auto self = Ref<VersusHubLayer>(this);
    VersusClient::get().authenticate([self, next = std::move(next)](bool ok, std::string const& message) {
        if (!self->isRunning()) return;
        self->m_busy = false;

        if (!ok) {
            self->setStatus(Localization::get().getString("versus.auth-failed"), true);
            log::warn("[Versus][Hub] Auth failed: {}", message);
            return;
        }
        self->refreshRank();
        next();
    });
}

void VersusHubLayer::onSessionChanged() {
    auto& session = VersusSession::get();
    auto const phase = session.phase();

    // a friendly joined by code lands past the accept step, so the modal opens
    // for any of the three lobby phases and not only for found.
    bool const inLobby = phase == Phase::Found || phase == Phase::Banning ||
                         phase == Phase::Loading;
    if (inLobby && !m_matchPopupOpen) {
        m_matchPopupOpen = true;
        if (auto* popup = VersusMatchPopup::create()) popup->show();
    }
    if (session.idle()) m_matchPopupOpen = false;

    refreshRank();
    refreshPlayButton();
    if (session.idle()) return;

    if (phase == Phase::Queued) {
        auto const& ticket = session.ticket();
        setStatus(fmt::format(fmt::runtime(Localization::get().getString("versus.searching")),
                              ticket.waiting, ticket.estimateSeconds));
        return;
    }
    setStatus(session.statusLine());
}

void VersusHubLayer::onPlay(CCObject*) {
    if (m_busy) return;

    // already queued: this button cancels instead of stacking a second search.
    if (VersusSession::get().phase() == Phase::Queued) {
        VersusSession::get().cancelQueue();
        setStatus(Localization::get().getString("versus.cancelled"));
        return;
    }

    // ranked needs the fast channel: the roulette feels like mud without it,
    // and a ladder with two qualities of experience is worse than one rule.
    if (!gl::connected()) {
        refreshGlobed();
        setStatus(Localization::get().getString("versus.globed-required"), true);
        return;
    }

    auto self = Ref<VersusHubLayer>(this);
    authenticateThen([self]() {
        VersusSession::get().beginQueue(self->m_mode,
                                        VersusStore::get().preferredFormat(self->m_mode));
    });
}

void VersusHubLayer::onFriendly(CCObject*) {
    if (m_busy) return;

    auto self = Ref<VersusHubLayer>(this);
    authenticateThen([self]() {
        if (auto* popup = VersusFriendlyPopup::create(self->m_mode)) popup->show();
    });
}

void VersusHubLayer::onDeck(CCObject*) {
    if (auto* popup = VersusDeckPopup::create()) popup->show();
}

void VersusHubLayer::onLeaderboard(CCObject*) {
    auto* scene = VersusLeaderboardLayer::scene(m_mode);
    if (!scene) return;
    VersusSession::get().removeListener(this);
    VersusSession::get().endWatch();
    CCDirector::get()->pushScene(CCTransitionFade::create(0.4f, scene));
}

void VersusHubLayer::onHistory(CCObject*) {
    if (auto* popup = VersusHistoryPopup::create()) popup->show();
}

void VersusHubLayer::onSeason(CCObject*) {
    if (auto* popup = VersusSeasonPopup::create()) popup->show();
}

void VersusHubLayer::onBack(CCObject*) {
    keyBackClicked();
}

void VersusHubLayer::keyBackClicked() {
    VersusSession::get().removeListener(this);
    VersusSession::get().endWatch();
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

} // namespace paimon::versus
