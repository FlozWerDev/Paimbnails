#include "IconSetRow.hpp"

#include "IconSetPreview.hpp"
#include "../IconCopyStore.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

// The big icon sits at the right end of the row and dissolves into the card as
// it goes left, so it reads as artwork instead of a second thumbnail.
constexpr float kMarkSize = 41.f;
constexpr float kMarkRight = 32.f;
constexpr float kFadeWidth = 58.f;

// Buttons stop here, clear of the artwork.
constexpr float kButtonGap = 8.f;
constexpr float kSmallButton = 26.f;

}  // anonymous namespace

CCNode* makeRowTextFace(char const* text, char const* buttonSprite) {
    return ButtonSprite::create(text, 46, true, "bigFont.fnt", buttonSprite, 22.f, 0.4f);
}

CCNode* makeRowIconFace(char const* frameName) {
    auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
    if (!spr) return nullptr;
    float const dim = std::max(spr->getContentWidth(), spr->getContentHeight());
    if (dim > 0.f) spr->setScale(kSmallButton / dim);
    return spr;
}

CCNode* makeSetRow(IconSet const& set, std::string const& subtitle, std::size_t index,
                   float width, std::vector<RowAction> const& actions) {
    ccColor3B const card = index % 2 == 0 ? ccColor3B{26, 26, 34} : ccColor3B{34, 34, 46};

    auto* row = CCNode::create();
    row->setContentSize({width, kRowHeight});

    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            width - 4.f, kRowHeight - 4.f, card, 255, 6.f)) {
        bg->setPosition({2.f, 2.f});
        row->addChild(bg);
    }

    if (auto* mark = makePreview(set, IconType::Cube, kMarkSize)) {
        mark->setPosition({width - kMarkRight, kRowHeight / 2.f});
        row->addChild(mark);
    }

    // Same colour as the card, opaque on the left and clear on the right, so the
    // icon under it dissolves towards the middle of the row.
    if (auto* fade = CCLayerGradient::create(
            ccc4(card.r, card.g, card.b, 255), ccc4(card.r, card.g, card.b, 0), ccp(1.f, 0.f))) {
        fade->setContentSize({kFadeWidth, kRowHeight - 4.f});
        fade->setPosition({width - kMarkRight - kMarkSize / 2.f - 6.f, 2.f});
        row->addChild(fade);
    }

    auto* name = CCLabelBMFont::create(set.label().c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->limitLabelWidth(115.f, 0.5f, 0.2f);
    name->setPosition({14.f, kRowHeight / 2.f + 8.f});
    row->addChild(name);

    auto* caption = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt");
    caption->setAnchorPoint({0.f, 0.5f});
    caption->setScale(0.4f);
    caption->setOpacity(140);
    caption->setPosition({14.f, kRowHeight / 2.f - 10.f});
    row->addChild(caption);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(row->getContentSize());
    row->addChild(menu, 2);

    // Collect first, place after: ButtonSprite rounds its absolute width up, so
    // the only way to keep real gaps is to measure what came out.
    std::vector<CCMenuItemSpriteExtra*> buttons;
    for (auto const& action : actions) {
        if (!action.face) continue;
        auto* btn = CCMenuItemExt::createSpriteExtra(action.face,
            [run = action.run](CCMenuItemSpriteExtra*) { if (run) run(); });
        if (!btn) continue;
        menu->addChild(btn);
        buttons.push_back(btn);
    }

    float const right = width - kMarkRight - kMarkSize / 2.f - 12.f;
    float total = 0.f;
    for (auto* btn : buttons) total += btn->getScaledContentSize().width + kButtonGap;
    total -= buttons.empty() ? 0.f : kButtonGap;

    float x = right - total;
    for (auto* btn : buttons) {
        float const w = btn->getScaledContentSize().width;
        btn->setPosition({x + w / 2.f, kRowHeight / 2.f});
        x += w + kButtonGap;
    }

    return row;
}

std::string formatSetDate(std::int64_t epoch) {
    if (epoch <= 0) return "unknown date";
    auto time = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return out.str();
}

}  // namespace paimon::iconcopy
