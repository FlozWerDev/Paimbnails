#include "LevelHistoryDetailPopup.hpp"
#include "InfoBlocks.hpp"
#include "../services/LevelFacts.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/GJDifficultySprite.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace paimon::info::blocks;

namespace paimon::info {

namespace {

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 280.f;
constexpr float kInnerW = 356.f;
constexpr float kHeaderH = 54.f;
constexpr float kListH = 144.f;
constexpr float kRowH = 21.f;

} // namespace

LevelHistoryDetailPopup* LevelHistoryDetailPopup::create(HistoryEntry const& entry) {
    auto ret = new LevelHistoryDetailPopup();
    if (ret && ret->init(entry)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelHistoryDetailPopup::init(HistoryEntry const& entry) {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    auto title = entry.levelName.empty() ? std::string("Registro") : entry.levelName;
    setTitle(title.c_str(), "goldFont.fnt", 0.62f, 20.f);
    if (m_title) m_title->limitLabelWidth(kInnerW - 60.f, 0.62f, 0.1f);

    float const left = (kPopupW - kInnerW) / 2.f;
    float const headerY = kPopupH - 42.f - kHeaderH;

    auto* header = makeBlock(kInnerW, kHeaderH, 80);
    header->setPosition({left, headerY});
    m_mainLayer->addChild(header);

    if (auto* face = GJDifficultySprite::create(entry.face, GJDifficultyName::Short)) {
        face->updateFeatureState(entry.feature);
        face->setScale(0.6f);
        face->setPosition({30.f, kHeaderH / 2.f + 3.f});
        header->addChild(face, 1);
    }

    float textX = 62.f;
    addText(header, entry.clock.empty()
                ? entry.date.c_str()
                : fmt::format("{}  {}", entry.date, entry.clock).c_str(),
            "goldFont.fnt", 0.44f, kValue, {textX, kHeaderH - 15.f}, {0.f, 0.5f}, kInnerW - textX - 10.f);

    auto subtitle = fmt::format("{}  -  #{}", entry.source, entry.recordID);
    if (entry.invalid) subtitle += "  -  invalido";
    addText(header, subtitle.c_str(), "chatFont.fnt", 0.38f, kLabel,
            {textX, kHeaderH - 34.f}, {0.f, 0.5f}, kInnerW - textX - 10.f);

    if (!entry.username.empty()) {
        addText(header, fmt::format("por {}", entry.username).c_str(), "chatFont.fnt", 0.38f,
                kAccent, {textX, 11.f}, {0.f, 0.5f}, kInnerW - textX - 10.f);
    }

    auto fields = describeEntry(entry);

    float const listY = headerY - kListH - 6.f;
    if (auto* panel = paimon::SpriteHelper::createDarkPanel(kInnerW, kListH, 70, 5.f)) {
        panel->setPosition({left, listY});
        m_mainLayer->addChild(panel);
    }

    auto* scroll = ScrollLayer::create({kInnerW, kListH});
    if (!scroll) return false;
    scroll->setPosition({left, listY});
    m_mainLayer->addChild(scroll, 2);

    float const totalH = std::max(kListH, kRowH * fields.size());
    auto* content = CCNode::create();
    content->setContentSize({kInnerW, totalH});
    scroll->m_contentLayer->addChild(content);
    scroll->m_contentLayer->setContentSize({kInnerW, totalH});

    float y = totalH - kRowH;
    for (size_t i = 0; i < fields.size(); i++) {
        auto* row = CCNode::create();
        row->setContentSize({kInnerW, kRowH});
        row->setPosition({0.f, y});

        // Bandas alternas: la lista es larga y sin ellas cuesta seguir la fila.
        if (i % 2 == 0) {
            if (auto* band = paimon::SpriteHelper::createDarkPanel(kInnerW - 6.f, kRowH - 2.f, 55, 3.f)) {
                band->setPosition({3.f, 1.f});
                row->addChild(band, -1);
            }
        }

        addText(row, fields[i].label.c_str(), "chatFont.fnt", 0.38f, kLabel,
                {10.f, kRowH / 2.f}, {0.f, 0.5f}, kInnerW * 0.44f);
        addText(row, fields[i].value.c_str(), "chatFont.fnt", 0.38f,
                fields[i].accent ? kAccent : kValue,
                {kInnerW - 10.f, kRowH / 2.f}, {1.f, 0.5f}, kInnerW * 0.5f);

        content->addChild(row);
        y -= kRowH;
    }

    scroll->scrollToTop();
    return true;
}

} // namespace paimon::info
