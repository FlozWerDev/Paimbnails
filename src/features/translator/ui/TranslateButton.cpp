#include "TranslateButton.hpp"

#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/ui/BasedButtonSprite.hpp>
#include <utility>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate::ui {

namespace {

constexpr int kBodyZ = 1;
constexpr int kGlyphZ = 2;

char const* busyBodyId() {
    static std::string const id = "translate-body-busy"_spr;
    return id.c_str();
}

char const* showingBodyId() {
    static std::string const id = "translate-body-showing"_spr;
    return id.c_str();
}

// same size as the base blank, so a body sits exactly over the blue one.
void addBody(CCSprite* base, CCSprite* body, char const* id) {
    if (!base || !body) return;
    auto const size = base->getContentSize();
    body->setID(id);
    body->setPosition({size.width * .5f, size.height * .5f});
    body->setVisible(false);
    base->addChild(body, kBodyZ);
}

CCSprite* makeAccountSprite() {
    CCNode* glyph = paimon::SpriteHelper::safeCreate("paim_ui_translate.png"_spr);
    if (!glyph) glyph = CCLabelBMFont::create("T", "bigFont.fnt");

    auto* base = AccountButtonSprite::create(glyph, AccountBaseColor::Blue);
    if (!base) return nullptr;
    base->setTopRelativeScale(1.05f);
    // the colour bodies go between the blue base and the bubbles
    if (auto* top = base->getTopNode()) base->reorderChild(top, kGlyphZ);

    addBody(base, paimon::SpriteHelper::safeCreateWithFrameName(
        "geode.loader/baseAccount_Normal_Gray.png"), busyBodyId());
    addBody(base, paimon::SpriteHelper::safeCreate("paim_ui_accountBaseGreen.png"_spr),
        showingBodyId());
    return base;
}

}

CCMenuItemSpriteExtra* makeTranslateButton(std::function<void()> onPress, float scale) {
    CCSprite* sprite = makeAccountSprite();
    if (sprite) {
        sprite->setScale(scale);
    } else {
        // the Geode blank is missing: keep the old text button so the feature still works
        sprite = paimon::ui::makeButtonSprite("T", paimon::ui::Btn::Blue, 0.f, .66f, "bigFont.fnt");
    }
    if (!sprite) return nullptr;

    return CCMenuItemExt::createSpriteExtra(sprite,
        [callback = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        });
}

void setTranslateButtonState(CCMenuItemSpriteExtra* button, TranslateButtonState state) {
    if (!button) return;
    button->setEnabled(state != TranslateButtonState::Busy);

    auto* image = button->getNormalImage();
    if (!image) return;

    if (typeinfo_cast<ButtonSprite*>(image)) {
        paimon::ui::setButtonSkin(button,
            state == TranslateButtonState::Showing ? paimon::ui::Btn::Green
            : state == TranslateButtonState::Busy  ? paimon::ui::Btn::Gray
                                                   : paimon::ui::Btn::Blue);
        return;
    }

    if (auto* busy = image->getChildByID(busyBodyId())) {
        busy->setVisible(state == TranslateButtonState::Busy);
    }
    if (auto* showing = image->getChildByID(showingBodyId())) {
        showing->setVisible(state == TranslateButtonState::Showing);
    }
}

}
