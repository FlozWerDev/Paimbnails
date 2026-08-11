#include "LevelTagsGatePopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::foryou {

namespace {
constexpr float kPopupWidth = 350.f;
constexpr float kPopupHeight = 216.f;
}

LevelTagsGatePopup* LevelTagsGatePopup::create(std::function<void()> onContinue) {
    auto ret = new LevelTagsGatePopup();
    if (ret && ret->init(std::move(onContinue))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void LevelTagsGatePopup::openModPage() {
    // Resolves to the installed mod's popup when present, otherwise fetches the
    // server entry and opens it with an Install button. Geode surfaces its own
    // error popup if the ID can't be found, so there is nothing to fall back to.
    geode::openInfoPopup(std::string(kLevelTagsModID));
}

bool LevelTagsGatePopup::init(std::function<void()> onContinue) {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    paimon::markDynamicPopup(this);

    m_onContinue = std::move(onContinue);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(Localization::get().getString("foryou.tags_gate_title").c_str());

    // The explanation sits in a recessed GD panel rather than floating on the
    // brown backdrop, matching how the rest of the game frames body text.
    float const panelW = kPopupWidth - 40.f;
    float const panelH = 86.f;
    float const panelTop = content.height - 42.f;

    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
        panel->setContentSize({panelW, panelH});
        panel->setAnchorPoint({0.5f, 1.f});
        panel->setPosition({cx, panelTop});
        panel->setColor({26, 20, 14});
        panel->setOpacity(125);
        m_mainLayer->addChild(panel, -1);
    }
    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({panelW, panelH});
        border->setAnchorPoint({0.5f, 1.f});
        border->setPosition({cx, panelTop});
        border->setOpacity(70);
        m_mainLayer->addChild(border, -1);
    }

    auto body = SimpleTextArea::create(
        Localization::get().getString("foryou.tags_gate_body"),
        "chatFont.fnt", 0.55f, panelW - 22.f);
    if (body) {
        body->setAlignment(kCCTextAlignmentCenter);
        body->setAnchorPoint({0.5f, 1.f});
        body->setPosition({cx, panelTop - 10.f});
        m_mainLayer->addChild(body);
    }

    // Install leads, so the recommended path is the obvious one.
    auto installSpr = ButtonSprite::create(
        Localization::get().getString("foryou.tags_gate_install").c_str(),
        110, true, "bigFont.fnt", "GJ_button_01.png", 26.f, 0.6f);
    if (installSpr) {
        installSpr->setScale(0.78f);
        auto installBtn = CCMenuItemSpriteExtra::create(
            installSpr, this, menu_selector(LevelTagsGatePopup::onInstall));
        installBtn->setPosition({cx, 58.f});
        installBtn->m_scaleMultiplier = 1.05f;
        m_buttonMenu->addChild(installBtn);
    }

    auto laterSpr = ButtonSprite::create(
        Localization::get().getString("foryou.tags_gate_later").c_str(),
        90, true, "bigFont.fnt", "GJ_button_04.png", 26.f, 0.6f);
    if (laterSpr) {
        laterSpr->setScale(0.62f);
        auto laterBtn = CCMenuItemSpriteExtra::create(
            laterSpr, this, menu_selector(LevelTagsGatePopup::onContinueWithout));
        laterBtn->setPosition({cx, 24.f});
        laterBtn->m_scaleMultiplier = 1.05f;
        m_buttonMenu->addChild(laterBtn);
    }

    return true;
}

void LevelTagsGatePopup::onInstall(CCObject*) {
    // Close first so the mod page isn't stacked behind our popup.
    auto continueCallback = m_onContinue;
    this->onClose(nullptr);
    openModPage();
    if (continueCallback) continueCallback();
}

void LevelTagsGatePopup::onContinueWithout(CCObject*) {
    auto continueCallback = m_onContinue;
    this->onClose(nullptr);
    if (continueCallback) continueCallback();
}

} // namespace paimon::foryou
