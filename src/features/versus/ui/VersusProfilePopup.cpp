#include "VersusProfilePopup.hpp"
#include "VersusHistoryPopup.hpp"
#include "VersusRankBadgeNode.hpp"
#include "../data/VersusRanks.hpp"
#include "../services/VersusClient.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 340.f;
constexpr float kPopupH = 250.f;

} // namespace

VersusProfilePopup* VersusProfilePopup::create(int accountId, std::string const& username,
                                               ModeProfile const& classic,
                                               ModeProfile const& platformer, bool own) {
    auto ret = new VersusProfilePopup();
    if (ret && ret->init(accountId, username, classic, platformer, own)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusProfilePopup::init(int accountId, std::string const& username,
                              ModeProfile const& classic, ModeProfile const& platformer, bool own) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_accountId = accountId;
    m_username = username;
    m_own = own;

    paimon::markDynamicPopup(this);
    this->setTitle(username.empty() ? Localization::get().getString("versus.profile.title") : username);

    buildColumn(Mode::Classic, classic, kPopupW * 0.28f);
    buildColumn(Mode::Platformer, platformer, kPopupW * 0.72f);

    auto* menu = CCMenu::create();
    menu->setPosition({kPopupW / 2.f, 24.f});
    m_mainLayer->addChild(menu, 3);

    auto& loc = Localization::get();
    if (own) {
        auto* face = ButtonSprite::create(loc.getString("versus.history.title").c_str(), 110, true,
                                          "bigFont.fnt", "GJ_button_04.png", 28.f, 0.5f);
        auto* btn = CCMenuItemSpriteExtra::create(face, this,
                                                  menu_selector(VersusProfilePopup::onHistory));
        menu->addChild(btn);
    } else {
        auto* face = ButtonSprite::create(loc.getString("versus.profile.challenge").c_str(), 110, true,
                                          "bigFont.fnt", "GJ_button_01.png", 28.f, 0.5f);
        auto* btn = CCMenuItemSpriteExtra::create(face, this,
                                                  menu_selector(VersusProfilePopup::onChallenge));
        menu->addChild(btn);
    }
    return true;
}

void VersusProfilePopup::buildColumn(Mode mode, ModeProfile const& profile, float centerX) {
    auto const rank = rankFor(profile.elo, profile.placementsLeft, profile.paimon);
    bool const played = profile.wins + profile.losses > 0;

    auto* heading = CCLabelBMFont::create(
        mode == Mode::Platformer ? "Platformer" : "Classic", "goldFont.fnt");
    heading->setScale(0.46f);
    heading->setPosition({centerX, kPopupH - 60.f});
    m_mainLayer->addChild(heading, 2);

    if (auto* badge = VersusRankBadgeNode::create(rank, 68.f)) {
        badge->setPosition({centerX, kPopupH - 110.f});
        badge->setDim(!played);
        m_mainLayer->addChild(badge, 2);
    }

    if (!played) {
        auto* none = CCLabelBMFont::create(
            Localization::get().getString("versus.profile.none").c_str(), "chatFont.fnt");
        none->setScale(0.44f);
        none->setOpacity(150);
        none->setPosition({centerX, kPopupH - 160.f});
        m_mainLayer->addChild(none, 2);
        return;
    }

    auto* name = CCLabelBMFont::create(rankName(rank).c_str(), "bigFont.fnt");
    name->setScale(std::min(0.48f, 130.f / std::max(1.f, name->getContentSize().width)));
    name->setColor(rankColor(rank));
    name->setPosition({centerX, kPopupH - 158.f});
    m_mainLayer->addChild(name, 2);

    auto* elo = CCLabelBMFont::create(
        rank.placing()
            ? fmt::format(fmt::runtime(Localization::get().getString("versus.placements-left")),
                          rank.placementsLeft).c_str()
            : fmt::format("{} Elo", profile.elo).c_str(),
        "chatFont.fnt");
    elo->setScale(0.46f);
    elo->setPosition({centerX, kPopupH - 178.f});
    m_mainLayer->addChild(elo, 2);

    auto* record = CCLabelBMFont::create(
        fmt::format("{}W - {}L", profile.wins, profile.losses).c_str(), "chatFont.fnt");
    record->setScale(0.46f);
    record->setOpacity(190);
    record->setPosition({centerX, kPopupH - 196.f});
    m_mainLayer->addChild(record, 2);
}

void VersusProfilePopup::onHistory(CCObject*) {
    if (auto* popup = VersusHistoryPopup::create()) popup->show();
}

void VersusProfilePopup::onChallenge(CCObject*) {
    if (m_username.empty()) return;

    auto const mode = VersusStore::get().preferredMode();
    auto const format = VersusStore::get().preferredFormat(mode);

    VersusClient::get().challenge(m_username, mode, format,
        [](bool ok, ChallengeResult const& result, std::string const& message) {
            if (!ok || result.matchId.empty()) {
                PaimonNotify::show(
                    message.empty()
                        ? Localization::get().getString("versus.challenge-failed") : message,
                    NotificationIcon::Error);
                return;
            }
            PaimonNotify::show(Localization::get().getString("versus.challenge-sent").c_str(),
                               NotificationIcon::Success);
        });

    Popup::onClose(nullptr);
}

} // namespace paimon::versus
