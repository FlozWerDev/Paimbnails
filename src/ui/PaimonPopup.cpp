#include "PaimonPopup.hpp"
#include "PaimonUI.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/SpriteHelper.hpp"
#include <algorithm>
#include <cstring>

using namespace cocos2d;
using namespace geode::prelude;

bool PaimonPopup::init(float width, float height, char const*, CCRect) {
    if (!Popup::init(width, height, "square02b_001.png")) return false;
    paimon::markDynamicPopup(this);
    m_noElasticity = true;
    m_bgSprite->setColor(paimon::ui::palette::surface);
    m_bgSprite->setOpacity(255);

    auto* shadow = paimon::ui::makeSurface({width + 8.f, height + 8.f}, {0, 0, 0}, 65, 12.f);
    shadow->setPosition({-4.f, -7.f});
    shadow->setID("paimon-popup-shadow"_spr);
    m_mainLayer->addChild(shadow, -2);

    auto* frame = paimon::SpriteHelper::createRoundedRectOutline(width, height, 8.f,
        {55.f / 255.f, 72.f / 255.f, 99.f / 255.f, 0.6f}, 0.65f);
    frame->setID("paimon-popup-surface"_spr);
    m_mainLayer->addChild(frame, 1);

    auto* accent = CCLayerColor::create({116, 204, 255, 125}, std::max(1.f, width - 32.f), 1.f);
    accent->setPosition({16.f, height - 2.f});
    m_mainLayer->addChild(accent, 1);

    auto* face = paimon::ui::makeButtonFace("x", {24.f, 24.f});
    m_closeBtn->setSprite(face);
    m_closeBtn->m_scaleMultiplier = 1.05f;
    return true;
}

bool PaimonPopup::init(CCSize size, char const* bg, CCRect bgRect) {
    return init(size.width, size.height, bg, bgRect);
}

void PaimonPopup::setTitle(ZStringView title, char const* font, float scale, float offset) {
    if (font && std::strcmp(font, "goldFont.fnt") == 0) font = "bigFont.fnt";
    scale = std::min(scale, 0.52f);
    Popup::setTitle(title, font, scale, offset);
    m_title->setColor(paimon::ui::palette::text);
    m_title->limitLabelWidth(std::max(1.f, m_size.width - 54.f), scale, 0.16f);
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
