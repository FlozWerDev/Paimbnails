#include "ProfilePicIconsDetailPopup.hpp"
#include "ProfilePicEditorPopup.hpp"
#include "../services/ProfilePicRenderer.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    constexpr float kW = 380.f;
    constexpr float kH = 280.f;

    CCLabelBMFont* sLbl(std::string const& str, float scale = 0.38f, char const* font = "goldFont.fnt") {
        auto lbl = CCLabelBMFont::create(str.c_str(), font);
        if (lbl) lbl->setScale(scale);
        return lbl;
    }
}

ProfilePicIconsDetailPopup* ProfilePicIconsDetailPopup::create(ProfilePicConfig* cfg, std::function<void()> onChange) {
    auto* ret = new ProfilePicIconsDetailPopup();
    if (ret && ret->init(cfg, onChange)) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

bool ProfilePicIconsDetailPopup::init(ProfilePicConfig* cfg, std::function<void()> onChange) {
    if (!Popup::init(kW, kH)) return false;
    this->setTitle("Icon Settings");
    m_cfg = cfg;
    m_onChange = onChange;
    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({kW - 20.f, kH - 50.f});
    m_contentNode->setAnchorPoint({0.5f, 0.5f});
    m_contentNode->setPosition({kW * 0.5f, kH * 0.5f - 10.f});
    m_mainLayer->addChild(m_contentNode);
    rebuild();
    return true;
}

void ProfilePicIconsDetailPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
}

// Build a live icon preview node
static CCNode* makeIconPreview(ProfilePicConfig* cfg, float size) {
    if (!cfg) return nullptr;
    ProfilePicConfig previewCfg = *cfg;
    previewCfg.onlyIconMode = true;
    previewCfg.scaleX = 1.f;
    previewCfg.scaleY = 1.f;
    previewCfg.rotation = 0.f;
    previewCfg.frameEnabled = false;
    return paimon::profile_pic::composeProfilePicture(nullptr, size, previewCfg);
}

void ProfilePicIconsDetailPopup::rebuild() {
    if (!m_contentNode) return;
    m_contentNode->removeAllChildren();

    auto area = m_contentNode->getContentSize();
    float topY = area.height - 8.f;
    float cx = area.width * 0.5f;

    // --- Icon Preview (top-right) ---
    float previewY = topY - 48.f;
    CCPoint previewCenter = {area.width - 52.f, previewY};
    auto previewBox = paimon::SpriteHelper::createColorPanel(80.f, 80.f, {40,40,40}, 160, 4.f);
    if (previewBox) {
        previewBox->setAnchorPoint({0.5f, 0.5f});
        previewBox->ignoreAnchorPointForPosition(false);
        previewBox->setPosition(previewCenter);
        m_contentNode->addChild(previewBox);
    }
    m_previewNode = CCNode::create();
    m_previewNode->setContentSize({80.f, 80.f});
    m_previewNode->setAnchorPoint({0.5f, 0.5f});
    m_previewNode->ignoreAnchorPointForPosition(false);
    m_previewNode->setPosition(previewCenter);
    m_contentNode->addChild(m_previewNode, 1);
    rebuildPreview();

    // --- Color 1 ---
    float y = topY;
    auto c1Lbl = sLbl("Color 1", 0.38f);
    c1Lbl->setAnchorPoint({0.f, 0.5f});
    c1Lbl->setPosition({10.f, y});
    m_contentNode->addChild(c1Lbl);

    auto c1Menu = CCMenu::create();
    c1Menu->setAnchorPoint({0.5f, 0.5f});
    c1Menu->setPosition({cx - 30.f, y});
    c1Menu->setContentSize({180.f, 18.f});
    m_contentNode->addChild(c1Menu);
    c1Menu->setLayout(RowLayout::create()->setGap(4.f)->setAutoScale(false));

    for (int i = 0; i < 2; i++) {
        bool sel = (i == 0 && m_cfg->iconConfig.colorSource == IconColorSource::Custom)
                || (i == 1 && m_cfg->iconConfig.colorSource == IconColorSource::Player);
        auto spr = ButtonSprite::create(i == 0 ? "Custom" : "Player", "goldFont.fnt",
            sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.6f);
        spr->setScale(0.36f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicIconsDetailPopup::onColor1Source));
        btn->setTag(i);
        c1Menu->addChild(btn);
    }
    // Pick color button (only when custom)
    if (m_cfg->iconConfig.colorSource == IconColorSource::Custom) {
        auto pickSpr = ButtonSprite::create("Pick", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        pickSpr->setScale(0.36f);
        auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ProfilePicIconsDetailPopup::onPickColor1));
        c1Menu->addChild(pickBtn);
    }
    c1Menu->updateLayout();

    // --- Color 2 ---
    y -= 24.f;
    auto c2Lbl = sLbl("Color 2", 0.38f);
    c2Lbl->setAnchorPoint({0.f, 0.5f});
    c2Lbl->setPosition({10.f, y});
    m_contentNode->addChild(c2Lbl);

    auto c2Menu = CCMenu::create();
    c2Menu->setAnchorPoint({0.5f, 0.5f});
    c2Menu->setPosition({cx - 30.f, y});
    c2Menu->setContentSize({180.f, 18.f});
    m_contentNode->addChild(c2Menu);
    c2Menu->setLayout(RowLayout::create()->setGap(4.f)->setAutoScale(false));

    for (int i = 0; i < 2; i++) {
        bool sel = (i == 0 && m_cfg->iconConfig.colorSource == IconColorSource::Custom)
                || (i == 1 && m_cfg->iconConfig.colorSource == IconColorSource::Player);
        auto spr = ButtonSprite::create(i == 0 ? "Custom" : "Player", "goldFont.fnt",
            sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.6f);
        spr->setScale(0.36f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicIconsDetailPopup::onColor2Source));
        btn->setTag(i);
        c2Menu->addChild(btn);
    }
    if (m_cfg->iconConfig.colorSource == IconColorSource::Custom) {
        auto pickSpr = ButtonSprite::create("Pick", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        pickSpr->setScale(0.36f);
        auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ProfilePicIconsDetailPopup::onPickColor2));
        c2Menu->addChild(pickBtn);
    }
    c2Menu->updateLayout();

    // --- Glow (single compact row) ---
    y -= 24.f;
    auto gMenu = CCMenu::create();
    gMenu->setPosition({22.f, y});
    m_contentNode->addChild(gMenu);
    auto gToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ProfilePicIconsDetailPopup::onGlowToggle), 0.55f);
    gToggle->toggle(m_cfg->iconConfig.glowEnabled);
    gMenu->addChild(gToggle);

    auto gLbl = sLbl("Glow", 0.36f, "bigFont.fnt");
    gLbl->setAnchorPoint({0.f, 0.5f});
    gLbl->setPosition({38.f, y});
    m_contentNode->addChild(gLbl);

    if (m_cfg->iconConfig.glowEnabled) {
        auto gcMenu = CCMenu::create();
        gcMenu->setAnchorPoint({0.5f, 0.5f});
        gcMenu->setPosition({cx + 20.f, y});
        gcMenu->setContentSize({140.f, 18.f});
        m_contentNode->addChild(gcMenu);
        gcMenu->setLayout(RowLayout::create()->setGap(3.f)->setAutoScale(false));
        for (int i = 0; i < 2; i++) {
            bool sel = (i == 0 && m_cfg->iconConfig.glowColorSource == IconColorSource::Custom)
                    || (i == 1 && m_cfg->iconConfig.glowColorSource == IconColorSource::Player);
            auto spr = ButtonSprite::create(i == 0 ? "Custom" : "Player", "goldFont.fnt",
                sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.6f);
            spr->setScale(0.34f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicIconsDetailPopup::onGlowColorSource));
            btn->setTag(i);
            gcMenu->addChild(btn);
        }
        if (m_cfg->iconConfig.glowColorSource == IconColorSource::Custom) {
            auto pickSpr = ButtonSprite::create("Pick", "goldFont.fnt", "GJ_button_04.png", 0.6f);
            pickSpr->setScale(0.34f);
            auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ProfilePicIconsDetailPopup::onPickGlowColor));
            gcMenu->addChild(pickBtn);
        }
        gcMenu->updateLayout();
    }

    // --- Scale ---
    y -= 24.f;
    auto scLbl = sLbl("Scale", 0.36f);
    scLbl->setAnchorPoint({0.f, 0.5f});
    scLbl->setPosition({10.f, y});
    m_contentNode->addChild(scLbl);
    auto scSlider = Slider::create(this, menu_selector(ProfilePicIconsDetailPopup::onScaleChanged), 0.55f);
    scSlider->setPosition({cx - 10.f, y});
    scSlider->setValue(std::clamp(m_cfg->iconConfig.scale / 2.f, 0.f, 1.f));
    scSlider->setAnchorPoint({0.5f, 0.5f});
    m_contentNode->addChild(scSlider);
    m_scaleSlider = scSlider;
    auto scVal = sLbl(fmt::format("{:.1f}", m_cfg->iconConfig.scale), 0.36f);
    scVal->setAnchorPoint({1.f, 0.5f});
    scVal->setPosition({area.width - 90.f, y});
    m_contentNode->addChild(scVal);
    m_scaleLabel = scVal;

    // --- Animation ---
    y -= 20.f;
    auto aLbl = sLbl("Anim", 0.36f);
    aLbl->setAnchorPoint({0.f, 0.5f});
    aLbl->setPosition({10.f, y});
    m_contentNode->addChild(aLbl);
    static const char* anims[] = {"None", "Zoom", "Rotate", "Both", "Flip X", "Flip Y", "Shake", "Bounce"};
    auto aMenu = CCMenu::create();
    aMenu->setAnchorPoint({0.5f, 0.5f});
    aMenu->setPosition({cx + 20.f, y - 8.f});
    aMenu->setContentSize({area.width - 60.f, 38.f});
    m_contentNode->addChild(aMenu);
    aMenu->setLayout(
        RowLayout::create()->setGap(3.f)->setAutoScale(false)
            ->setGrowCrossAxis(true)->setCrossAxisOverflow(false)
            ->setCrossAxisReverse(false)
    );
    for (int i = 0; i < 8; i++) {
        bool sel = m_cfg->iconConfig.animationType == i;
        auto spr = ButtonSprite::create(anims[i], "goldFont.fnt",
            sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.6f);
        spr->setScale(0.32f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicIconsDetailPopup::onAnimTypeSelect));
        btn->setTag(i);
        aMenu->addChild(btn);
    }
    aMenu->updateLayout();
    y -= 20.f;

    // --- Animation Speed / Duration ---
    y -= 22.f;
    auto spLbl = sLbl("Time", 0.34f);
    spLbl->setAnchorPoint({0.f, 0.5f});
    spLbl->setPosition({10.f, y});
    m_contentNode->addChild(spLbl);
    auto spSlider = Slider::create(this, menu_selector(ProfilePicIconsDetailPopup::onAnimSpeedChanged), 0.5f);
    spSlider->setPosition({cx - 10.f, y});
    float currentDur = (m_cfg->iconConfig.animationSpeed > 0.f) ? (1.0f / m_cfg->iconConfig.animationSpeed) : 1.0f;
    spSlider->setValue(std::clamp((currentDur - 0.2f) / (3.0f - 0.2f), 0.f, 1.f));
    spSlider->setAnchorPoint({0.5f, 0.5f});
    m_contentNode->addChild(spSlider);
    m_speedSlider = spSlider;
    auto spVal = sLbl(fmt::format("{:.1f}", currentDur), 0.34f);
    spVal->setAnchorPoint({1.f, 0.5f});
    spVal->setPosition({area.width - 90.f, y});
    m_contentNode->addChild(spVal);
    m_speedLabel = spVal;

    // --- Custom Icons (compact) ---
    y -= 22.f;
    auto ciLbl = sLbl("Custom", 0.36f);
    ciLbl->setAnchorPoint({0.f, 0.5f});
    ciLbl->setPosition({10.f, y});
    m_contentNode->addChild(ciLbl);
    auto addSpr = ButtonSprite::create("Add", "goldFont.fnt", "GJ_button_04.png", 0.6f);
    addSpr->setScale(0.34f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(ProfilePicIconsDetailPopup::onAddCustomIcon));
    auto addMenu = CCMenu::create();
    addMenu->setPosition({area.width - 90.f, y});
    addMenu->addChild(addBtn);
    m_contentNode->addChild(addMenu);

    if (!m_cfg->customIcons.empty()) {
        y -= 20.f;
        auto ciMenu = CCMenu::create();
        ciMenu->setAnchorPoint({0.5f, 0.5f});
        ciMenu->setPosition({cx - 10.f, y});
        ciMenu->setContentSize({area.width - 110.f, 18.f});
        m_contentNode->addChild(ciMenu);
        ciMenu->setLayout(RowLayout::create()->setGap(3.f)->setAutoScale(false));
        for (size_t i = 0; i < m_cfg->customIcons.size(); i++) {
            bool sel = m_cfg->selectedCustomIconIndex == (int)i;
            auto cellBg = paimon::SpriteHelper::createColorPanel(22.f, 18.f,
                sel ? ccColor3B{60,160,60} : ccColor3B{40,40,40}, sel ? 200 : 120, 2.f);
            auto path = m_cfg->customIcons[i].path;
            CCSprite* icon = nullptr;
            if (!path.empty()) icon = CCSprite::create(path.c_str());
            if (icon) {
                float md = std::max(icon->getContentWidth(), icon->getContentHeight());
                if (md > 0) icon->setScale(14.f / md);
                icon->setAnchorPoint({0.5f, 0.5f});
                icon->setPosition({11.f, 9.f});
                cellBg->addChild(icon);
            } else {
                auto ql = CCLabelBMFont::create("?", "bigFont.fnt");
                ql->setScale(0.24f); ql->setAnchorPoint({0.5f,0.5f}); ql->setPosition({11.f,9.f}); cellBg->addChild(ql);
            }
            auto btn = CCMenuItemSpriteExtra::create(cellBg, this, menu_selector(ProfilePicIconsDetailPopup::onCustomIconSelect));
            btn->setTag((int)i);
            ciMenu->addChild(btn);
        }
        ciMenu->updateLayout();
        if (m_cfg->selectedCustomIconIndex >= 0) {
            y -= 18.f;
            auto rmSpr = ButtonSprite::create("Remove", "goldFont.fnt", "GJ_button_06.png", 0.6f);
            rmSpr->setScale(0.32f);
            auto rmBtn = CCMenuItemSpriteExtra::create(rmSpr, this, menu_selector(ProfilePicIconsDetailPopup::onRemoveCustomIcon));
            auto rmMenu = CCMenu::create();
            rmMenu->setPosition({cx - 10.f, y});
            rmMenu->addChild(rmBtn);
            m_contentNode->addChild(rmMenu);
        }
    }

    // --- Icon Image Toggle ---
    y -= 24.f;
    auto iiMenu = CCMenu::create();
    iiMenu->setPosition({20.f, y});
    m_contentNode->addChild(iiMenu);
    auto iiToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ProfilePicIconsDetailPopup::onIconImageToggle), 0.55f);
    iiToggle->toggle(m_cfg->iconConfig.iconImageEnabled);
    iiMenu->addChild(iiToggle);
    auto iiLbl = sLbl("Icon on BG", 0.32f, "chatFont.fnt");
    iiLbl->setColor({180,180,200}); iiLbl->setOpacity(200);
    iiLbl->setAnchorPoint({0.f, 0.5f});
    iiLbl->setPosition({36.f, y});
    m_contentNode->addChild(iiLbl);
}

void ProfilePicIconsDetailPopup::rebuildPreview() {
    if (!m_previewNode) return;
    m_previewNode->removeAllChildrenWithCleanup(true);
    if (auto preview = makeIconPreview(m_cfg, 56.f)) {
        preview->setAnchorPoint({0.5f, 0.5f});
        preview->ignoreAnchorPointForPosition(false);
        preview->setPosition({40.f, 40.f});
        m_previewNode->addChild(preview);
    }
}

// ---- Handlers ----

void ProfilePicIconsDetailPopup::onColor1Source(CCObject* sender) {
    int tag = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    m_cfg->iconConfig.colorSource = (tag == 0) ? IconColorSource::Custom : IconColorSource::Player;
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onPickColor1(CCObject*) {
    auto* pop = geode::ColorPickPopup::create(m_cfg->iconConfig.color1);
    if (!pop) return;
    pop->setCallback([this](ccColor4B const& c){ m_cfg->iconConfig.color1 = {c.r,c.g,c.b}; rebuild(); if (m_onChange) m_onChange(); });
    pop->show();
}
void ProfilePicIconsDetailPopup::onColor2Source(CCObject* sender) {
    int tag = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    m_cfg->iconConfig.colorSource = (tag == 0) ? IconColorSource::Custom : IconColorSource::Player;
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onPickColor2(CCObject*) {
    auto* pop = geode::ColorPickPopup::create(m_cfg->iconConfig.color2);
    if (!pop) return;
    pop->setCallback([this](ccColor4B const& c){ m_cfg->iconConfig.color2 = {c.r,c.g,c.b}; rebuild(); if (m_onChange) m_onChange(); });
    pop->show();
}
void ProfilePicIconsDetailPopup::onGlowToggle(CCObject*) {
    m_cfg->iconConfig.glowEnabled = !m_cfg->iconConfig.glowEnabled;
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onGlowColorSource(CCObject* sender) {
    int tag = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    m_cfg->iconConfig.glowColorSource = (tag == 0) ? IconColorSource::Custom : IconColorSource::Player;
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onPickGlowColor(CCObject*) {
    auto* pop = geode::ColorPickPopup::create(m_cfg->iconConfig.glowColor);
    if (!pop) return;
    pop->setCallback([this](ccColor4B const& c){ m_cfg->iconConfig.glowColor = {c.r,c.g,c.b}; rebuild(); if (m_onChange) m_onChange(); });
    pop->show();
}
void ProfilePicIconsDetailPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider) return;
    m_cfg->iconConfig.scale = m_scaleSlider->getValue() * 2.f;
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("{:.1f}", m_cfg->iconConfig.scale).c_str());
    rebuildPreview();
    if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onAnimSpeedChanged(CCObject*) {
    if (!m_speedSlider) return;
    float dur = 0.2f + m_speedSlider->getValue() * (3.0f - 0.2f);
    m_cfg->iconConfig.animationSpeed = (dur > 0.01f) ? (1.0f / dur) : 1.0f;
    if (m_speedLabel) m_speedLabel->setString(fmt::format("{:.1f}", dur).c_str());
    rebuildPreview();
    if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onAddCustomIcon(CCObject*) {
    PaimonNotify::create("Drag & drop image files to mod folder", NotificationIcon::Info)->show();
}
void ProfilePicIconsDetailPopup::onRemoveCustomIcon(CCObject*) {
    if (m_cfg->selectedCustomIconIndex >= 0 && m_cfg->selectedCustomIconIndex < (int)m_cfg->customIcons.size()) {
        m_cfg->customIcons.erase(m_cfg->customIcons.begin() + m_cfg->selectedCustomIconIndex);
        m_cfg->selectedCustomIconIndex = -1;
    }
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onCustomIconSelect(CCObject* sender) {
    m_cfg->selectedCustomIconIndex = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onAnimTypeSelect(CCObject* sender) {
    m_cfg->iconConfig.animationType = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    rebuild(); if (m_onChange) m_onChange();
}
void ProfilePicIconsDetailPopup::onIconImageToggle(CCObject*) {
    m_cfg->iconConfig.iconImageEnabled = !m_cfg->iconConfig.iconImageEnabled;
    rebuild(); if (m_onChange) m_onChange();
}
