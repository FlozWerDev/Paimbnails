#include "VersusMatchPopup.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusModes.hpp"
#include "../data/VersusRanks.hpp"
#include "../services/VersusSession.hpp"
#include "../services/VersusStore.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/GameManager.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 290.f;
constexpr int kOfferTag = 6100;

char const* difficultySprite(int difficulty) {
    switch (std::clamp(difficulty, 1, 6)) {
        case 1:  return "diffIcon_01_btn_001.png";
        case 2:  return "diffIcon_02_btn_001.png";
        case 3:  return "diffIcon_03_btn_001.png";
        case 4:  return "diffIcon_04_btn_001.png";
        case 5:  return "diffIcon_05_btn_001.png";
        default: return "diffIcon_06_btn_001.png";
    }
}

// one side of the card: badge, name and the record under it, drawn the same for
// both players so the modal reads as a mirror.
CCNode* buildSide(std::string const& name, RankInfo const& rank, int wins, int losses) {
    auto* side = CCNode::create();

    if (auto* badge = VersusRankBadgeNode::create(rank, 54.f)) {
        badge->setShowPips(false);
        badge->setPosition({0.f, 0.f});
        side->addChild(badge, 1);
    }

    auto* label = CCLabelBMFont::create(name.c_str(), "goldFont.fnt");
    label->setScale(std::min(0.52f, 130.f / std::max(1.f, label->getContentSize().width)));
    label->setPosition({0.f, -36.f});
    side->addChild(label, 1);

    auto* record = CCLabelBMFont::create(
        fmt::format("{} - {}W {}L", rankName(rank), wins, losses).c_str(), "chatFont.fnt");
    record->setScale(std::min(0.42f, 140.f / std::max(1.f, record->getContentSize().width)));
    record->setOpacity(190);
    record->setPosition({0.f, -52.f});
    side->addChild(record, 1);

    return side;
}

} // namespace

VersusMatchPopup* VersusMatchPopup::create() {
    auto ret = new VersusMatchPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusMatchPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);
    this->setTitle(Localization::get().getString("versus.match.title"));

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_menu, 5);

    rebuild();
    return true;
}

void VersusMatchPopup::onEnter() {
    Popup::onEnter();

    auto self = Ref<VersusMatchPopup>(this);
    VersusSession::get().addListener(this, [self]() {
        if (self->isRunning()) self->rebuild();
    });
}

void VersusMatchPopup::onExit() {
    VersusSession::get().removeListener(this);
    Popup::onExit();
}

void VersusMatchPopup::rebuild() {
    auto const phase = VersusSession::get().phase();

    // the level takes over from here; idle and finished have no modal.
    if (phase == Phase::Countdown || phase == Phase::Running ||
        phase == Phase::Idle || phase == Phase::Finished) {
        Popup::onClose(nullptr);
        return;
    }
    // a veto lands without the phase moving, so the offers are part of what
    // counts as already drawn.
    uint32_t const offers = offerStamp();
    if (phase == m_drawn && offers == m_drawnOffers && m_page) return;

    m_drawn = phase;
    m_drawnOffers = offers;
    if (m_page) m_page->removeFromParent();
    m_menu->removeAllChildren();

    m_page = CCNode::create();
    m_page->setContentSize({kPopupW, kPopupH});
    m_mainLayer->addChild(m_page, 2);

    buildSteps(m_page, phase);

    switch (phase) {
        case Phase::Found:   buildFound(m_page); break;
        case Phase::Banning: buildBanning(m_page); break;
        default:             buildLoading(m_page); break;
    }
}

uint32_t VersusMatchPopup::offerStamp() const {
    uint32_t stamp = 0;
    for (auto const& offer : VersusSession::get().match().offers) {
        stamp = stamp * 31u + static_cast<uint32_t>(offer.levelId) + (offer.banned ? 1u : 0u);
    }
    return stamp;
}

void VersusMatchPopup::buildSteps(CCNode* page, Phase phase) {
    auto& loc = Localization::get();

    char const* keys[] = {"versus.step.accept", "versus.step.ban", "versus.step.play"};
    int const current = phase == Phase::Found ? 0 : phase == Phase::Banning ? 1 : 2;

    for (int i = 0; i < 3; i++) {
        bool const active = i == current;
        float const x = kPopupW / 2.f + (i - 1) * 122.f;

        if (auto* chip = paimon::SpriteHelper::createColorPanel(
                112.f, 22.f, active ? ccColor3B{90, 74, 30} : ccColor3B{0, 0, 0},
                active ? 220 : 110, 4.f)) {
            chip->setPosition({x - 56.f, kPopupH - 62.f});
            page->addChild(chip, 1);
        }

        auto* label = CCLabelBMFont::create(
            fmt::format("{} {}", i + 1, loc.getString(keys[i])).c_str(), "bigFont.fnt");
        label->setScale(std::min(0.38f, 100.f / std::max(1.f, label->getContentSize().width)));
        label->setColor(active ? ui::kAccent : ccColor3B{150, 156, 172});
        label->setPosition({x, kPopupH - 51.f});
        page->addChild(label, 2);
    }
}

void VersusMatchPopup::buildFound(CCNode* page) {
    auto const& match = VersusSession::get().match();
    auto& loc = Localization::get();

    if (auto* burst = paimon::SpriteHelper::safeCreate("paim_vsBurst.png"_spr)) {
        burst->setScale(150.f / std::max(1.f, burst->getContentSize().width));
        burst->setPosition({kPopupW / 2.f, kPopupH - 116.f});
        burst->setColor(ui::kAccent);
        burst->setOpacity(60);
        burst->runAction(CCRepeatForever::create(CCRotateBy::create(18.f, 360.f)));
        page->addChild(burst, 0);
    }

    if (auto* logo = paimon::SpriteHelper::safeCreate("paim_vsLogo.png"_spr)) {
        logo->setScale(30.f / std::max(1.f, logo->getContentSize().height));
        logo->setPosition({kPopupW / 2.f, kPopupH - 116.f});
        logo->setColor(ui::kAccent);
        page->addChild(logo, 3);
    }

    auto const& profile = VersusStore::get().profile(match.mode);
    auto* own = buildSide(GameManager::sharedState()->m_playerName,
                          VersusStore::get().rank(match.mode), profile.wins, profile.losses);
    own->setPosition({kPopupW * 0.22f, kPopupH - 116.f});
    page->addChild(own, 2);

    auto* rival = buildSide(match.rival.name,
                            rankFor(match.rival.elo, match.rival.placementsLeft),
                            match.rival.wins, match.rival.losses);
    rival->setPosition({kPopupW * 0.78f, kPopupH - 116.f});
    page->addChild(rival, 2);

    auto const& def = formatAt(match.format);
    auto* format = CCLabelBMFont::create(
        fmt::format("{} - {}", formatName(def), formatWinCondition(def)).c_str(), "chatFont.fnt");
    format->setScale(std::min(0.46f, (kPopupW - 90.f) /
                                     std::max(1.f, format->getContentSize().width)));
    format->setPosition({kPopupW / 2.f + 16.f, kPopupH - 196.f});
    page->addChild(format, 2);

    if (auto* glyph = paimon::SpriteHelper::safeCreate(formatSprite(def).c_str())) {
        glyph->setScale(24.f / std::max(1.f, glyph->getContentSize().width));
        glyph->setPosition({kPopupW / 2.f + 16.f - format->getScaledContentSize().width / 2.f - 18.f,
                            kPopupH - 196.f});
        page->addChild(glyph, 2);
    }

    auto* stake = CCLabelBMFont::create(
        loc.getString(match.ranked ? "versus.stake.ranked" : "versus.stake.friendly").c_str(),
        "chatFont.fnt");
    stake->setScale(0.42f);
    stake->setColor(match.ranked ? ui::kAccent : ccColor3B{160, 210, 255});
    stake->setPosition({kPopupW / 2.f, kPopupH - 216.f});
    page->addChild(stake, 2);

    auto* accept = ui::makeAction(loc.getString("versus.match.accept"), 120, "GJ_button_01.png",
                                  0.6f, this, menu_selector(VersusMatchPopup::onAccept));
    accept->setPosition({kPopupW / 2.f - 72.f, 44.f});
    m_menu->addChild(accept);

    auto* decline = ui::makeAction(loc.getString("versus.match.decline"), 110, "GJ_button_06.png",
                                   0.5f, this, menu_selector(VersusMatchPopup::onDecline));
    decline->setPosition({kPopupW / 2.f + 72.f, 44.f});
    m_menu->addChild(decline);

    auto* warning = CCLabelBMFont::create(loc.getString("versus.match.dodge-warning").c_str(),
                                          "chatFont.fnt");
    warning->setScale(0.42f);
    warning->setOpacity(150);
    warning->setPosition({kPopupW / 2.f, 18.f});
    page->addChild(warning, 2);
}

void VersusMatchPopup::buildBanning(CCNode* page) {
    auto const& match = VersusSession::get().match();
    auto& loc = Localization::get();

    auto* hint = CCLabelBMFont::create(loc.getString("versus.match.ban-hint").c_str(), "goldFont.fnt");
    hint->setScale(0.48f);
    hint->setPosition({kPopupW / 2.f, kPopupH - 86.f});
    page->addChild(hint, 2);

    float const step = 124.f;
    for (size_t i = 0; i < match.offers.size(); i++) {
        auto const& offer = match.offers[i];
        float const x = kPopupW / 2.f + (static_cast<float>(i) - 1.f) * step;

        auto* card = CCNode::create();
        card->setContentSize({116.f, 112.f});
        card->setPosition({x, kPopupH - 166.f});
        page->addChild(card, 2);

        if (auto* panel = paimon::SpriteHelper::createDarkPanel(116.f, 112.f,
                                                                offer.banned ? 180 : 110, 5.f)) {
            panel->setPosition({-58.f, -56.f});
            card->addChild(panel, 0);
        }

        if (auto* diff = paimon::SpriteHelper::safeCreateWithFrameName(difficultySprite(offer.difficulty))) {
            diff->setScale(0.7f);
            diff->setPosition({0.f, 18.f});
            if (offer.banned) diff->setColor({90, 90, 100});
            card->addChild(diff, 1);
        }

        auto* name = CCLabelBMFont::create(offer.name.c_str(), "bigFont.fnt");
        name->setScale(std::min(0.42f, 100.f / std::max(1.f, name->getContentSize().width)));
        name->setPosition({0.f, -20.f});
        if (offer.banned) name->setColor({110, 110, 120});
        card->addChild(name, 1);

        auto* author = CCLabelBMFont::create(offer.author.c_str(), "chatFont.fnt");
        author->setScale(std::min(0.4f, 100.f / std::max(1.f, author->getContentSize().width)));
        author->setOpacity(offer.banned ? 100 : 180);
        author->setPosition({0.f, -38.f});
        card->addChild(author, 1);

        if (offer.banned) {
            auto* stamp = CCLabelBMFont::create(loc.getString("versus.match.banned").c_str(),
                                                "bigFont.fnt");
            stamp->setScale(0.46f);
            stamp->setColor(ui::kBad);
            stamp->setRotation(-14.f);
            stamp->setPosition({0.f, 0.f});
            card->addChild(stamp, 3);
            continue;
        }

        auto* btn = ui::makeAction(loc.getString("versus.match.ban"), 88, "GJ_button_06.png",
                                   0.44f, this, menu_selector(VersusMatchPopup::onBan));
        btn->setTag(kOfferTag + offer.levelId);
        btn->setPosition({x, kPopupH - 242.f});
        m_menu->addChild(btn);
    }

    auto* status = CCLabelBMFont::create(VersusSession::get().statusLine().c_str(), "chatFont.fnt");
    status->setScale(0.46f);
    status->setOpacity(170);
    status->setPosition({kPopupW / 2.f, 24.f});
    page->addChild(status, 2);
}

void VersusMatchPopup::buildLoading(CCNode* page) {
    auto const& match = VersusSession::get().match();
    auto& loc = Localization::get();

    auto* status = CCLabelBMFont::create(VersusSession::get().statusLine().c_str(), "goldFont.fnt");
    status->setScale(0.6f);
    status->setPosition({kPopupW / 2.f, kPopupH - 120.f});
    page->addChild(status, 2);

    auto const& def = formatAt(match.format);
    auto* rule = CCLabelBMFont::create(
        fmt::format("{} - {}", formatName(def), formatWinCondition(def)).c_str(), "chatFont.fnt");
    rule->setScale(std::min(0.46f, (kPopupW - 90.f) /
                                   std::max(1.f, rule->getContentSize().width)));
    rule->setOpacity(190);
    rule->setPosition({kPopupW / 2.f, kPopupH - 148.f});
    page->addChild(rule, 2);

    if (match.levelId == 0) return;

    auto* play = ui::makeAction(loc.getString("versus.match.play"), 150, "GJ_button_01.png", 0.7f,
                                this, menu_selector(VersusMatchPopup::onPlay));
    play->setPosition({kPopupW / 2.f, kPopupH - 200.f});
    m_menu->addChild(play);
}

void VersusMatchPopup::onAccept(CCObject*) {
    VersusSession::get().accept(true);
}

void VersusMatchPopup::onDecline(CCObject*) {
    VersusSession::get().accept(false);
    Popup::onClose(nullptr);
}

void VersusMatchPopup::onBan(CCObject* sender) {
    VersusSession::get().ban(sender->getTag() - kOfferTag);
}

void VersusMatchPopup::onPlay(CCObject*) {
    // close first: pushing the scene while the modal is still animating out
    // leaves the fade running over the level.
    Popup::onClose(nullptr);
    VersusSession::get().enterLevel();
}

} // namespace paimon::versus
