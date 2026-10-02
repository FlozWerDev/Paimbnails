#include "PaimonPopup.hpp"
#include "PaimonUI.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include <algorithm>

using namespace cocos2d;
using namespace geode::prelude;

bool PaimonPopup::init(float width, float height, char const* bg, CCRect bgRect) {
    if (!bg || bg[0] == '\0') bg = "GJ_square01.png";
    if (!Popup::init(width, height, bg, bgRect)) return false;
    // Popup adds the window at z 0; content insets use negative z and must stay above it.
    m_mainLayer->reorderChild(m_bgSprite, -10);
    paimon::markDynamicPopup(this);
    m_noElasticity = true;
    return true;
}

bool PaimonPopup::init(CCSize size, char const* bg, CCRect bgRect) {
    return init(size.width, size.height, bg, bgRect);
}

void PaimonPopup::setTitle(ZStringView title, char const*, float scale, float offset) {
    scale = std::min(scale, 0.75f);
    Popup::setTitle(title, "goldFont.fnt", scale, offset);
    m_title->limitLabelWidth(std::max(1.f, m_size.width - 70.f), scale, 0.2f);
}

CCMenuItemSpriteExtra* PaimonPopup::addInfoButton(std::string const& title, std::string const& body,
    Anchor anchor, CCPoint offset) {
    auto* button = paimon::ui::makeInfoButton(title, body, 0.6f);
    if (!button) return nullptr;
    button->setID("info-button"_spr);
    m_buttonMenu->addChildAtPosition(button, anchor, offset);
    return button;
}

void PaimonPopup::addCorners(SideArtStyle style, float scale, bool top) {
    paimon::ui::addCorners(m_mainLayer, m_size, style, scale, top);
}

void PaimonPopup::show() {
    if (!getParent() && m_mainLayer) {
        auto const win = CCDirector::get()->getWinSize();
        auto const size = m_mainLayer->getScaledContentSize();
        float const fit = std::min({1.f, (win.width - 28.f) / std::max(1.f, size.width),
            (win.height - 28.f) / std::max(1.f, size.height)});
        m_mainLayer->setScaleX(m_mainLayer->getScaleX() * fit);
        m_mainLayer->setScaleY(m_mainLayer->getScaleY() * fit);
    }
    FLAlertLayer::show();
}
