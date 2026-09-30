#include "VersusHistoryPopup.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusModes.hpp"
#include "../services/VersusStore.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <ctime>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 280.f;
constexpr float kListW = 376.f;
constexpr float kListH = 188.f;
constexpr float kRowH = 38.f;
constexpr int kModeTag = 7300;

ccColor3B outcomeColor(Outcome outcome) {
    switch (outcome) {
        case Outcome::Win:  return {140, 230, 160};
        case Outcome::Loss: return {240, 130, 140};
        case Outcome::Void: return {200, 160, 240};
        default:            return {230, 220, 160};
    }
}

char const* outcomeLetter(Outcome outcome) {
    switch (outcome) {
        case Outcome::Win:  return "W";
        case Outcome::Loss: return "L";
        case Outcome::Void: return "-";
        default:            return "D";
    }
}

// days since the match, which reads better than a date in a list this short.
std::string agoLabel(int64_t playedAt) {
    if (playedAt <= 0) return {};
    auto const now = static_cast<int64_t>(std::time(nullptr));
    auto const days = (now - playedAt) / 86400;
    if (days <= 0) return Localization::get().getString("versus.history.today");
    return fmt::format(fmt::runtime(Localization::get().getString("versus.history.days")), days);
}

} // namespace

VersusHistoryPopup* VersusHistoryPopup::create() {
    auto ret = new VersusHistoryPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusHistoryPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);
    this->setTitle(Localization::get().getString("versus.history.title"));

    m_mode = VersusStore::get().preferredMode();

    auto* menu = CCMenu::create();
    menu->setPosition({kPopupW / 2.f, kPopupH - 52.f});
    m_mainLayer->addChild(menu, 3);

    char const* labels[] = {"Classic", "Platformer"};
    for (int i = 0; i < 2; i++) {
        auto* btn = ui::makeTab(labels[i], 78.f, this, menu_selector(VersusHistoryPopup::onMode));
        btn->setTag(kModeTag + i);
        btn->setPosition({(i - 0.5f) * 84.f, 0.f});
        menu->addChild(btn);
        m_modeButtons.push_back(btn);
    }

    auto* legend = ui::makeText(Localization::get().getString("versus.history.legend"),
                                "chatFont.fnt", 0.4f, {kPopupW / 2.f, kPopupH - 76.f});
    legend->setOpacity(160);
    m_mainLayer->addChild(legend, 3);

    m_scroll = ScrollLayer::create({kListW, kListH});
    m_scroll->setPosition({(kPopupW - kListW) / 2.f, 14.f});
    m_mainLayer->addChild(m_scroll, 1);

    auto* borders = ListBorders::create();
    borders->setContentSize({kListW, kListH});
    borders->setPosition({kPopupW / 2.f, 14.f + kListH / 2.f});
    m_mainLayer->addChild(borders, 4);

    buildRows();
    return true;
}

void VersusHistoryPopup::onMode(CCObject* sender) {
    auto const mode = static_cast<Mode>(sender->getTag() - kModeTag);
    if (mode == m_mode) return;
    m_mode = mode;
    buildRows();
}

void VersusHistoryPopup::buildRows() {
    for (size_t i = 0; i < m_modeButtons.size(); i++) {
        ui::styleTab(m_modeButtons[i], static_cast<int>(i) == static_cast<int>(m_mode));
    }

    m_scroll->m_contentLayer->removeAllChildren();

    std::vector<MatchRecord> shown;
    for (auto const& record : VersusStore::get().history()) {
        if (record.mode == m_mode) shown.push_back(record);
    }

    if (shown.empty()) {
        auto* empty = CCLabelBMFont::create(
            Localization::get().getString("versus.history.empty").c_str(), "chatFont.fnt");
        empty->setScale(0.5f);
        empty->setOpacity(160);
        empty->setPosition({kListW / 2.f, kListH / 2.f});
        m_scroll->m_contentLayer->setContentSize({kListW, kListH});
        m_scroll->m_contentLayer->addChild(empty, 1);
        return;
    }

    float const height = std::max(kListH, shown.size() * kRowH);
    m_scroll->m_contentLayer->setContentSize({kListW, height});

    for (size_t i = 0; i < shown.size(); i++) {
        auto const& record = shown[i];
        float const y = height - kRowH * (i + 0.5f);

        auto* strip = CCLayerColor::create(
            i % 2 ? ccColor4B{0, 0, 0, 60} : ccColor4B{0, 0, 0, 95}, kListW, kRowH - 2.f);
        strip->setPosition({0.f, y - (kRowH - 2.f) / 2.f});
        m_scroll->m_contentLayer->addChild(strip, 0);

        auto* mark = CCLabelBMFont::create(outcomeLetter(record.outcome), "bigFont.fnt");
        mark->setScale(0.6f);
        mark->setColor(outcomeColor(record.outcome));
        mark->setPosition({22.f, y});
        m_scroll->m_contentLayer->addChild(mark, 1);

        auto* rival = CCLabelBMFont::create(record.rival.c_str(), "bigFont.fnt");
        rival->setAnchorPoint({0.f, 0.5f});
        rival->setScale(std::min(0.44f, 150.f / std::max(1.f, rival->getContentSize().width)));
        rival->setPosition({44.f, y + 7.f});
        m_scroll->m_contentLayer->addChild(rival, 1);

        // rows written before the duel kept the score have none to show.
        std::string score;
        if (record.ownPercent > 0.f || record.rivalPercent > 0.f) {
            score = fmt::format("{}% / {}% - ", static_cast<int>(record.ownPercent),
                                static_cast<int>(record.rivalPercent));
        }

        auto* detail = CCLabelBMFont::create(
            fmt::format("{} - {}{}", formatName(formatAt(record.format)), score,
                        agoLabel(record.playedAt)).c_str(),
            "chatFont.fnt");
        detail->setAnchorPoint({0.f, 0.5f});
        detail->setScale(std::min(0.4f, 260.f / std::max(1.f, detail->getContentSize().width)));
        detail->setOpacity(175);
        detail->setPosition({44.f, y - 9.f});
        m_scroll->m_contentLayer->addChild(detail, 1);

        if (record.eloDelta != 0) {
            auto* delta = CCLabelBMFont::create(
                fmt::format("{}{}", record.eloDelta > 0 ? "+" : "", record.eloDelta).c_str(),
                "bigFont.fnt");
            delta->setAnchorPoint({1.f, 0.5f});
            delta->setScale(0.46f);
            delta->setColor(record.eloDelta > 0 ? ccColor3B{140, 230, 160} : ccColor3B{240, 130, 140});
            delta->setPosition({kListW - 12.f, y});
            m_scroll->m_contentLayer->addChild(delta, 1);
        }
    }

    m_scroll->moveToTop();
}

} // namespace paimon::versus
