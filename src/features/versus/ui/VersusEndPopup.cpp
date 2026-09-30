#include "VersusEndPopup.hpp"
#include "../data/VersusModes.hpp"
#include "../services/VersusClient.hpp"
#include "../services/VersusSession.hpp"
#include "../services/VersusStore.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 340.f;
constexpr float kPopupH = 250.f;

ccColor3B outcomeColor(Outcome outcome) {
    switch (outcome) {
        case Outcome::Win:  return {140, 230, 160};
        case Outcome::Loss: return {240, 130, 140};
        case Outcome::Void: return {200, 160, 240};
        default:            return {230, 220, 160};
    }
}

char const* outcomeKey(Outcome outcome) {
    switch (outcome) {
        case Outcome::Win:  return "versus.result.win";
        case Outcome::Loss: return "versus.result.loss";
        case Outcome::Void: return "versus.result.void";
        default:            return "versus.result.draw";
    }
}

} // namespace

VersusEndPopup* VersusEndPopup::create() {
    auto ret = new VersusEndPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusEndPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    this->setTitle(Localization::get().getString("versus.end.title"));
    paimon::markDynamicPopup(this);

    buildResult();
    buildRankStrip();
    buildButtons();
    return true;
}

void VersusEndPopup::buildResult() {
    auto const& session = VersusSession::get();
    auto& loc = Localization::get();

    auto* verdict = CCLabelBMFont::create(loc.getString(outcomeKey(session.outcome())).c_str(),
                                          "bigFont.fnt");
    verdict->setScale(0.9f);
    verdict->setColor(outcomeColor(session.outcome()));
    verdict->setPosition({kPopupW / 2.f, kPopupH - 70.f});
    m_mainLayer->addChild(verdict, 2);

    auto const& own = session.own();
    auto const& rival = session.rival();
    auto* score = CCLabelBMFont::create(
        fmt::format("{:.1f}%   -   {:.1f}%", own.bestPercent, rival.bestPercent).c_str(),
        "goldFont.fnt");
    score->setScale(0.6f);
    score->setPosition({kPopupW / 2.f, kPopupH - 98.f});
    m_mainLayer->addChild(score, 2);

    auto* names = CCLabelBMFont::create(
        fmt::format("{}   vs   {}",
                    GameManager::sharedState()->m_playerName,
                    session.match().rival.name).c_str(),
        "chatFont.fnt");
    names->setScale(0.5f);
    names->setOpacity(190);
    names->setPosition({kPopupW / 2.f, kPopupH - 118.f});
    m_mainLayer->addChild(names, 2);
}

void VersusEndPopup::buildRankStrip() {
    auto const& session = VersusSession::get();
    auto& store = VersusStore::get();
    auto const rank = store.rank(session.match().mode);

    m_badge = VersusRankBadgeNode::create(rank, 64.f);
    m_badge->setPosition({64.f, 92.f});
    m_mainLayer->addChild(m_badge, 2);

    auto* name = CCLabelBMFont::create(rankName(rank).c_str(), "goldFont.fnt");
    name->setScale(0.5f);
    name->setColor(rankColor(rank));
    name->setPosition({64.f, 50.f});
    m_mainLayer->addChild(name, 2);

    // the server, not the format, decides whether this moved anything: a
    // friendly played under race is still a friendly.
    int const delta = session.eloDelta();
    auto* eloLabel = CCLabelBMFont::create(
        session.match().ranked
            ? fmt::format("{}{} Elo", delta >= 0 ? "+" : "", delta).c_str()
            : Localization::get().getString("versus.end.unranked").c_str(),
        "bigFont.fnt");
    eloLabel->setScale(0.62f);
    eloLabel->setColor(delta >= 0 ? ccColor3B{140, 230, 160} : ccColor3B{240, 130, 140});
    eloLabel->setPosition({kPopupW / 2.f + 60.f, 96.f});
    m_mainLayer->addChild(eloLabel, 2);

    auto* total = CCLabelBMFont::create(
        fmt::format("{} Elo", store.profile(session.match().mode).elo).c_str(), "chatFont.fnt");
    total->setScale(0.5f);
    total->setOpacity(190);
    total->setPosition({kPopupW / 2.f + 60.f, 72.f});
    m_mainLayer->addChild(total, 2);

    if (rank.placing()) {
        auto* placement = CCLabelBMFont::create(
            fmt::format(fmt::runtime(Localization::get().getString("versus.placements-left")),
                        rank.placementsLeft).c_str(),
            "chatFont.fnt");
        placement->setScale(0.46f);
        placement->setOpacity(170);
        placement->setPosition({kPopupW / 2.f + 60.f, 52.f});
        m_mainLayer->addChild(placement, 2);
    } else if (delta > 0) {
        m_badge->playPromotion();
    }
}

void VersusEndPopup::buildButtons() {
    auto& loc = Localization::get();

    auto* menu = CCMenu::create();
    menu->setPosition({kPopupW / 2.f, 22.f});
    m_mainLayer->addChild(menu, 3);

    auto* againFace = ButtonSprite::create(loc.getString("versus.end.rematch").c_str(), 110, true,
                                           "bigFont.fnt", "GJ_button_01.png", 28.f, 0.5f);
    auto* again = CCMenuItemSpriteExtra::create(againFace, this,
                                                menu_selector(VersusEndPopup::onRematch));
    again->setPosition({-62.f, 0.f});
    menu->addChild(again);

    auto* closeFace = ButtonSprite::create(loc.getString("versus.end.leave").c_str(), 90, true,
                                           "bigFont.fnt", "GJ_button_04.png", 28.f, 0.5f);
    auto* close = CCMenuItemSpriteExtra::create(closeFace, this,
                                                menu_selector(VersusEndPopup::onClose));
    close->setPosition({62.f, 0.f});
    menu->addChild(close);

    if (auto* reportFace = paimon::SpriteHelper::safeCreateWithFrameName("GJ_reportBtn_001.png")) {
        reportFace->setScale(0.55f);
        auto* report = CCMenuItemSpriteExtra::create(reportFace, this,
                                                     menu_selector(VersusEndPopup::onReport));
        report->setPosition({kPopupW / 2.f - 18.f, 4.f});
        menu->addChild(report);
    }
}

void VersusEndPopup::onRematch(CCObject*) {
    auto const& session = VersusSession::get();
    auto const mode = session.match().mode;
    auto const format = session.match().format;
    auto const rival = session.match().rival.name;
    bool const ranked = session.match().ranked;

    VersusSession::get().reset();

    // a friendly goes back to the same person; the queue would hand it to a
    // stranger under rules it never offers.
    if (!ranked && !rival.empty()) {
        VersusClient::get().challenge(rival, mode, format,
            [](bool ok, ChallengeResult const&, std::string const& message) {
                PaimonNotify::show(
                    ok ? Localization::get().getString("versus.challenge-sent")
                       : (message.empty()
                            ? Localization::get().getString("versus.challenge-failed") : message),
                    ok ? NotificationIcon::Success : NotificationIcon::Error);
            });
        Popup::onClose(nullptr);
        return;
    }

    VersusSession::get().beginQueue(mode, format);
    PaimonNotify::show(Localization::get().getString("versus.status.queued").c_str(),
                       NotificationIcon::Info);
    Popup::onClose(nullptr);
}

void VersusEndPopup::onReport(CCObject*) {
    auto const matchId = VersusSession::get().match().id;
    if (matchId.empty()) return;

    VersusClient::get().reportPlayer(matchId, "", [](bool ok, std::string const& message) {
        PaimonNotify::show(
            ok ? Localization::get().getString("versus.report.sent").c_str() : message.c_str(),
            ok ? NotificationIcon::Success : NotificationIcon::Error);
    });
}

void VersusEndPopup::onClose(CCObject* sender) {
    VersusSession::get().reset();
    Popup::onClose(sender);
}

} // namespace paimon::versus
