#include "ProfileBgGradientPopup.hpp"
#include "../services/ProfileGradientEffects.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include <Geode/binding/GameManager.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
constexpr float kSpeedMin   = 0.1f;
constexpr float kSpeedMax   = 5.0f;
constexpr float kSpeedRange = kSpeedMax - kSpeedMin;

float speedFromSlider(float v) {
    return kSpeedMin + std::clamp(v, 0.f, 1.f) * kSpeedRange;
}

float speedToSlider(float speed) {
    if (kSpeedRange <= 0.f) return 0.f;
    float v = (speed - kSpeedMin) / kSpeedRange;
    return std::clamp(v, 0.f, 1.f);
}

CCSprite* spriteForEffect(std::string const& effect) {
    auto try_ = [&](char const* primary, char const* fallback) -> CCSprite* {
        auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(primary);
        if (!spr && fallback) spr = paimon::SpriteHelper::safeCreateWithFrameName(fallback);
        if (!spr) spr = CCSprite::create();
        return spr;
    };
    if (effect == "none")   return try_("GJ_deleteIcon_001.png", "GJ_deleteBtn_001.png");
    if (effect == "rotate") return try_("GJ_replayBtn_001.png",  "GJ_undoBtn_001.png");
    if (effect == "pulse")  return try_("GJ_likeBtn_001.png",    "GJ_starBtn_001.png");
    if (effect == "shift")  return try_("GJ_paintBtn_001.png",   "GJ_colorBtn_001.png");
    if (effect == "slide")  return try_("GJ_compactPlayBtn_001.png", "GJ_playBtn_001.png");
    return try_("GJ_paintBtn_001.png", "GJ_colorBtn_001.png");
}
} // namespace

ProfileBgGradientPopup* ProfileBgGradientPopup::create(int accountID,
                                                       std::string const& initialEffect,
                                                       float initialSpeed,
                                                       ApplyCallback onApply) {
    auto ret = new ProfileBgGradientPopup();
    if (ret && ret->init(accountID, initialEffect, initialSpeed, std::move(onApply))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileBgGradientPopup::init(int accountID,
                                  std::string const& initialEffect,
                                  float initialSpeed,
                                  ApplyCallback onApply) {
    if (!Popup::init(420.f, 270.f)) return false;

    m_accountID = accountID;
    m_effect    = paimon::profilebg::normalizeEffect(initialEffect);
    m_speed     = paimon::profilebg::normalizeSpeed(initialSpeed);
    m_onApply   = std::move(onApply);

    this->setTitle(Localization::get().getString("profilebg.gradient.title").c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // ── Preview area ──────────────────────────────────────────────
    float previewY = content.height - 90.f;
    float previewW = 260.f;
    float previewH = 60.f;

    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({previewW, previewH});
    m_previewContainer->setAnchorPoint({0.5f, 0.5f});
    m_previewContainer->ignoreAnchorPointForPosition(false);
    m_previewContainer->setPosition({cx, previewY});
    m_mainLayer->addChild(m_previewContainer);

    rebuildPreview();

    // ── Effect buttons ────────────────────────────────────────────
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 5);

    float btnY = previewY - 60.f;
    auto const& effects = paimon::profilebg::availableEffects();
    int n = (int)effects.size();
    float spacing = 70.f;
    float startX  = cx - spacing * (n - 1) * 0.5f;

    for (int i = 0; i < n; ++i) {
        auto const& effectId = effects[i];

        auto spr = spriteForEffect(effectId);
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(28.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ProfileBgGradientPopup::onSelectEffect)
        );
        btn->setPosition({startX + spacing * i, btnY});
        btn->setUserObject(CCString::create(effectId.c_str()));
        menu->addChild(btn);

        m_effectButtons.emplace_back(effectId, btn);

        // Etiqueta debajo
        std::string label = Localization::get().getString("profilebg.gradient.effect." + effectId);
        if (label.empty() || label == ("profilebg.gradient.effect." + effectId)) {
            label = effectId;
        }
        auto* lbl = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
        lbl->setScale(0.30f);
        lbl->setPosition({startX + spacing * i, btnY - 22.f});
        m_mainLayer->addChild(lbl);
    }

    refreshEffectSelection();

    // ── Speed slider ──────────────────────────────────────────────
    float sliderY = btnY - 60.f;

    auto* speedTitle = CCLabelBMFont::create(
        Localization::get().getString("profilebg.gradient.speed").c_str(),
        "bigFont.fnt"
    );
    speedTitle->setScale(0.34f);
    speedTitle->setPosition({cx - 130.f, sliderY});
    speedTitle->setAnchorPoint({0, 0.5f});
    m_mainLayer->addChild(speedTitle);

    m_speedSlider = Slider::create(this, menu_selector(ProfileBgGradientPopup::onSpeedChanged), 1.0f);
    m_speedSlider->setPosition({cx, sliderY});
    m_speedSlider->setValue(speedToSlider(m_speed));
    m_mainLayer->addChild(m_speedSlider);

    m_speedLabel = CCLabelBMFont::create("1.0x", "bigFont.fnt");
    m_speedLabel->setScale(0.34f);
    m_speedLabel->setPosition({cx + 130.f, sliderY});
    m_speedLabel->setAnchorPoint({1, 0.5f});
    m_mainLayer->addChild(m_speedLabel);
    refreshSpeedLabel();

    // ── Apply button ──────────────────────────────────────────────
    auto applySpr = ButtonSprite::create(
        Localization::get().getString("profilebg.gradient.apply").c_str(),
        "bigFont.fnt", "GJ_button_01.png", 0.7f
    );
    auto applyBtn = CCMenuItemSpriteExtra::create(
        applySpr, this, menu_selector(ProfileBgGradientPopup::onApplyBtn)
    );
    applyBtn->setPosition({cx, 28.f});
    menu->addChild(applyBtn);

    // ── Info button ───────────────────────────────────────────────
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(20.f / maxDim);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ProfileBgGradientPopup::onInfo)
        );
        btn->setPosition({content.width - 16.f, content.height - 16.f});
        menu->addChild(btn);
    }

    this->setZOrder(10510);
    this->setID("profile-bg-gradient-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

ccColor3B ProfileBgGradientPopup::currentPlayerColor1() {
    auto* gm = GameManager::sharedState();
    if (!gm) return {255, 255, 255};
    return gm->colorForIdx(gm->getPlayerColor());
}

ccColor3B ProfileBgGradientPopup::currentPlayerColor2() {
    auto* gm = GameManager::sharedState();
    if (!gm) return {255, 255, 255};
    return gm->colorForIdx(gm->getPlayerColor2());
}

void ProfileBgGradientPopup::rebuildPreview() {
    if (!m_previewContainer) return;
    m_previewContainer->removeAllChildrenWithCleanup(true);

    auto sz = m_previewContainer->getContentSize();
    auto colorA = currentPlayerColor1();
    auto colorB = currentPlayerColor2();

    // Stencil rectangular para clipping del preview animado
    auto stencil = paimon::SpriteHelper::createRectStencil(sz.width, sz.height);
    auto clip = CCClippingNode::create();
    clip->setStencil(stencil);
    clip->setContentSize(sz);
    clip->setAnchorPoint({0, 0});
    clip->setPosition({0, 0});
    m_previewContainer->addChild(clip);

    auto* grad = paimon::profilebg::AnimatedGradientLayer::create(colorA, colorB);
    if (grad) {
        grad->setContentSize(sz);
        grad->setAnchorPoint({0.5f, 0.5f});
        grad->ignoreAnchorPointForPosition(false);
        grad->setPosition({sz.width * 0.5f, sz.height * 0.5f});
        clip->addChild(grad);
        grad->setEffect(m_effect, m_speed);
    }

    // Borde estetico
    auto border = CCLayerColor::create(ccc4(255, 255, 255, 60));
    border->setContentSize(sz);
    border->setAnchorPoint({0, 0});
    border->setPosition({0, 0});
    border->setVisible(false); // borde oscuro: dejamos solo gradient
    m_previewContainer->addChild(border);
}

void ProfileBgGradientPopup::refreshEffectSelection() {
    for (auto& [id, btn] : m_effectButtons) {
        if (!btn) continue;
        auto* spr = typeinfo_cast<CCSprite*>(btn->getNormalImage());
        if (!spr) {
            // Igual ajustamos la escala como feedback visual aunque no
            // podamos teñir el sprite.
            btn->setScale(id == m_effect ? 1.15f : 1.0f);
            continue;
        }
        if (id == m_effect) {
            spr->setColor({255, 255, 255});
            btn->setScale(1.15f);
        } else {
            spr->setColor({160, 160, 160});
            btn->setScale(1.0f);
        }
    }
}

void ProfileBgGradientPopup::refreshSpeedLabel() {
    if (!m_speedLabel) return;
    m_speedLabel->setString(fmt::format("{:.1f}x", m_speed).c_str());
}

void ProfileBgGradientPopup::onSelectEffect(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* tag = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!tag) return;
    m_effect = paimon::profilebg::normalizeEffect(tag->getCString());
    refreshEffectSelection();
    rebuildPreview();
}

void ProfileBgGradientPopup::onSpeedChanged(CCObject* sender) {
    auto* slider = typeinfo_cast<Slider*>(sender);
    if (!slider) return;
    m_speed = paimon::profilebg::normalizeSpeed(speedFromSlider(slider->getValue()));
    refreshSpeedLabel();
    // No reconstruimos el preview entero por cada tick; solo re-seteamos el
    // efecto en el gradient existente para no recrear el nodo.
    if (m_previewContainer) {
        for (auto* child : CCArrayExt<CCNode*>(m_previewContainer->getChildren())) {
            auto* clip = typeinfo_cast<CCClippingNode*>(child);
            if (!clip) continue;
            for (auto* gchild : CCArrayExt<CCNode*>(clip->getChildren())) {
                if (auto* grad = typeinfo_cast<paimon::profilebg::AnimatedGradientLayer*>(gchild)) {
                    grad->setEffect(m_effect, m_speed);
                    return;
                }
            }
        }
    }
}

void ProfileBgGradientPopup::onApplyBtn(CCObject*) {
    auto cb = m_onApply;
    auto effect = m_effect;
    auto speed  = m_speed;
    this->onClose(nullptr);
    if (cb) cb(effect, speed);
}

void ProfileBgGradientPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        Localization::get().getString("profilebg.gradient.info_title").c_str(),
        Localization::get().getString("profilebg.gradient.info_body"),
        Localization::get().getString("profilesettings.info_ok").c_str()
    )->show();
}
