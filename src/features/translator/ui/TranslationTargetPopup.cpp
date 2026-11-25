#include "TranslationTargetPopup.hpp"

#include "TranslatorLanguageGrid.hpp"
#include "../TranslatorLanguages.hpp"
#include "../../../ui/PaimonUI.hpp"
#include <utility>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate {
namespace {

constexpr float kWidth = 380.f;
constexpr float kHeight = 180.f;
// the panel stays clear of the gold corners and the title
constexpr float kPanelMargin = 28.f;
constexpr float kPanelBottom = 26.f;
constexpr float kPanelHeight = 114.f;

constexpr float kTileWidth = 98.f;
constexpr float kTileHeight = 30.f;
constexpr float kColumnStep = 106.f;
constexpr float kRowStep = 36.f;

}

TranslationTargetPopup* TranslationTargetPopup::create(std::string title, std::string selected,
    geode::CopyableFunction<void(std::string const&)> onPick) {
    auto* popup = new TranslationTargetPopup();
    if (popup->init(std::move(title), std::move(selected), std::move(onPick))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool TranslationTargetPopup::init(std::string title, std::string selected,
    geode::CopyableFunction<void(std::string const&)> onPick) {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    m_onPick = std::move(onPick);

    setID("translation-target-popup"_spr);
    setTitle(title.c_str());
    // four gold corners, like GD's own news and reward windows
    addCorners(geode::SideArtStyle::PopupGold, 1.f, true);

    auto* panel = paimon::ui::makeInset({kWidth - kPanelMargin * 2.f, kPanelHeight}, 80);
    panel->setID("translation-target-panel"_spr);
    panel->setPosition({kPanelMargin, kPanelBottom});
    m_mainLayer->addChild(panel, 1);

    auto* menu = paimon::ui::makeZeroMenu("translation-target-menu"_spr);
    menu->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(menu, 3);

    auto const& entries = languages();
    std::size_t const rows = (entries.size() + 2) / 3;
    float const gridHeight = static_cast<float>(rows > 0 ? rows - 1 : 0) * kRowStep + kTileHeight;
    float const firstRowY = kPanelBottom + (kPanelHeight + gridHeight) * .5f - kTileHeight * .5f;

    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const& entry = entries[index];
        auto* tile = ui::makeLanguageTile(entry, selected == entry.code, kTileWidth);
        if (!tile) continue;

        auto* button = CCMenuItemExt::createSpriteExtra(tile,
            [this, code = std::string(entry.code)](CCMenuItemSpriteExtra*) {
                if (m_onPick) m_onPick(code);
                this->onClose(nullptr);
            });
        button->setID(std::string("translation-target-"_spr) + entry.code);
        button->setPosition({
            kWidth * .5f + (static_cast<float>(index % 3) - 1.f) * kColumnStep,
            firstRowY - static_cast<float>(index / 3) * kRowStep,
        });
        menu->addChild(button);
        paimon::ui::animateIn(button, .015f * static_cast<float>(index), 4.f);
    }
    return true;
}

}
