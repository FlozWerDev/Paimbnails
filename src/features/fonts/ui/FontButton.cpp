#include "FontButton.hpp"
#include "FontPickerPopup.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::fonts;

FontButton* FontButton::create(CopyableFunction<void(std::string const&)> insertFn) {
    auto ret = new FontButton();
    if (ret && ret->init(std::move(insertFn))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FontButton::init(CopyableFunction<void(std::string const&)> insertFn) {
    // Sprite vanilla de GD
    auto btnSpr = CCSprite::createWithSpriteFrameName("GJ_editObjBtn4_001.png");
    if (!btnSpr) {
        // Fallback: intentar como archivo
        btnSpr = CCSprite::create("GJ_editObjBtn4_001.png");
    }
    if (!btnSpr) {
        // Fallback final: boton plano de GD
        auto bs = ButtonSprite::create("Aa", 30, true, "chatFont.fnt", "GJ_plainBtn_001.png", 25.f, 0.5f);
        if (!bs) return false;
        if (!CCMenuItemSpriteExtra::init(bs, nullptr, this, menu_selector(FontButton::onToggle))) {
            return false;
        }
        m_insertFn = std::move(insertFn);
        this->setID("paimon-font-btn"_spr);
        PaimonButtonHighlighter::registerButton(this);
        return true;
    }

    // Label "Aa" centrado sobre el boton
    auto label = CCLabelBMFont::create("Aa", "chatFont.fnt");
    label->setScale(0.55f);
    label->setPosition({
        btnSpr->getContentSize().width / 2.f,
        btnSpr->getContentSize().height / 2.f
    });
    btnSpr->addChild(label, 5);

    if (!CCMenuItemSpriteExtra::init(btnSpr, nullptr, this, menu_selector(FontButton::onToggle))) {
        return false;
    }

    m_insertFn = std::move(insertFn);
    this->setID("paimon-font-btn"_spr);
    PaimonButtonHighlighter::registerButton(this);

    return true;
}

void FontButton::onToggle(CCObject*) {
    if (m_activePicker && m_activePicker->getParent()) {
        m_activePicker->removeFromParent();
        m_activePicker = nullptr;
        return;
    }
    m_activePicker = nullptr;

    auto picker = FontPickerPopup::create(
        [this](std::string const& fontTag) {
            if (m_insertFn) m_insertFn(fontTag);
        }
    );

    if (picker) {
        picker->show();
        picker->positionBelow(this, 0.f);
        m_activePicker = picker;
    }
}
