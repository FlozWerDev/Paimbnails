#include "TranslatorLanguageGrid.hpp"

#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>
#include <cctype>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate::ui {
namespace {

// tile-local units; the whole tile is scaled afterwards
constexpr float kBadgeWidth = 28.f;
constexpr float kBadgeHeight = 20.f;
constexpr float kBadgeLeft = 6.f;
constexpr float kNameLeft = kBadgeLeft + kBadgeWidth + 6.f;
constexpr float kNameScale = .62f;
constexpr float kCheckSpace = 20.f;
constexpr int kContentZ = 2;

std::string upperCode(char const* code) {
    std::string out = code ? code : "";
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

paimon::ui::Btn tileSkin(bool selected) {
    return selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Blue;
}

// the name gives up its right end to the check only on the selected tile.
void fitName(ButtonSprite* tile, bool selected) {
    auto* name = typeinfo_cast<CCLabelBMFont*>(tile->getChildByID("language-name"_spr));
    if (!name) return;
    float const right = tile->getContentSize().width - (selected ? kCheckSpace : 6.f);
    name->limitLabelWidth(std::max(10.f, right - kNameLeft), kNameScale, .1f);
}

}

ButtonSprite* makeLanguageTile(LanguageEntry const& entry, bool selected, float width,
    float scale) {
    auto* tile = paimon::ui::makeButtonSprite("", tileSkin(selected), width, scale, "bigFont.fnt");
    if (!tile) return nullptr;
    auto const size = tile->getContentSize();
    float const midY = size.height * .5f;

    auto* badge = paimon::ui::makeInset({kBadgeWidth, kBadgeHeight}, 110);
    badge->setPosition({kBadgeLeft, midY - kBadgeHeight * .5f});
    tile->addChild(badge, kContentZ);

    auto* code = CCLabelBMFont::create(upperCode(entry.code).c_str(), "goldFont.fnt");
    code->limitLabelWidth(kBadgeWidth - 6.f, .5f, .1f);
    code->setPosition({kBadgeLeft + kBadgeWidth * .5f, midY + 1.f});
    tile->addChild(code, kContentZ + 1);

    auto* name = CCLabelBMFont::create(entry.nativeName, "chatFont.fnt");
    name->setID("language-name"_spr);
    name->setAnchorPoint({0.f, .5f});
    name->setPosition({kNameLeft, midY + 1.f});
    tile->addChild(name, kContentZ);
    fitName(tile, selected);

    if (auto* check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_completesIcon_001.png")) {
        check->setID("language-check"_spr);
        check->setScale(.45f);
        check->setPosition({size.width - kCheckSpace * .5f - 1.f, midY});
        check->setVisible(selected);
        tile->addChild(check, kContentZ);
    }
    return tile;
}

void setLanguageTileSelected(CCMenuItemSpriteExtra* button, bool selected) {
    if (!button) return;
    paimon::ui::setButtonSkin(button, tileSkin(selected));
    auto* tile = typeinfo_cast<ButtonSprite*>(button->getNormalImage());
    if (!tile) return;
    if (auto* check = tile->getChildByID("language-check"_spr)) check->setVisible(selected);
    fitName(tile, selected);
}

}
