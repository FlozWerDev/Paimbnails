#include <Geode/ui/PopupManager.hpp>
#include "ProfileBgPickerPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

ProfileBgPickerPopup* ProfileBgPickerPopup::create(int accountID) {
    auto ret = new ProfileBgPickerPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileBgPickerPopup::init(int accountID) {
    if (!Popup::init(400.f, 200.f)) return false;

    m_accountID = accountID;

    this->setTitle(Localization::get().getString("profilebg.picker.title").c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float cy = content.height / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    constexpr float btnSpacing  = 80.f;
    const float    rowY        = cy + 8.f;
    constexpr float labelOffset = -32.f;

    auto makeBtn = [&](const char* label,
                       const char* primaryFrame,
                       const char* fallbackFrame,
                       float x,
                       SEL_MenuHandler selector,
                       cocos2d::ccColor3B labelColor) -> CCMenuItemSpriteExtra*
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(primaryFrame);
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName(fallbackFrame);
        if (!spr) spr = CCSprite::create();

        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(36.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, selector);
        btn->setPosition({x, rowY});
        menu->addChild(btn);

        auto lbl = CCLabelBMFont::create(label, "bigFont.fnt");
        lbl->setScale(0.34f);
        lbl->setColor(labelColor);
        lbl->setPosition({x, rowY + labelOffset});
        const float maxLabelWidth = btnSpacing - 6.f;
        if (lbl->getContentWidth() * lbl->getScale() > maxLabelWidth) {
            float fit = maxLabelWidth / std::max(1.f, lbl->getContentWidth());
            lbl->setScale(std::min(0.34f, fit));
        }
        m_mainLayer->addChild(lbl);

        return btn;
    };

    const float startX = cx - 1.5f * btnSpacing;

    makeBtn(
        Localization::get().getString("profilebg.picker.media").c_str(),
        "GJ_duplicateBtn_001.png", "GJ_editBtn_001.png",
        startX + 0.f * btnSpacing,
        menu_selector(ProfileBgPickerPopup::onPickMedia),
        {255, 255, 255}
    );

    makeBtn(
        Localization::get().getString("profilebg.picker.icon_gradient").c_str(),
        "GJ_paintBtn_001.png", "GJ_colorBtn_001.png",
        startX + 1.f * btnSpacing,
        menu_selector(ProfileBgPickerPopup::onPickGradient),
        {255, 255, 255}
    );

    makeBtn(
        Localization::get().getString("profilebg.picker.video_audio").c_str(),
        "GJ_audioOnBtn_001.png", "GJ_playMusicBtn_001.png",
        startX + 2.f * btnSpacing,
        menu_selector(ProfileBgPickerPopup::onPickVideoAudio),
        {200, 230, 255}
    );

    makeBtn(
        Localization::get().getString("profilebg.picker.reset").c_str(),
        "GJ_deleteBtn_001.png", "GJ_deleteIcon_001.png",
        startX + 3.f * btnSpacing,
        menu_selector(ProfileBgPickerPopup::onPickReset),
        {255, 200, 200}
    );

    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(20.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileBgPickerPopup::onInfo));
        btn->setPosition({content.width - 16.f, content.height - 16.f});
        menu->addChild(btn);
    }

    this->setZOrder(10500);
    this->setID("profile-bg-picker-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

void ProfileBgPickerPopup::onPickMedia(CCObject*) {
    auto cb = m_onPickMedia;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileBgPickerPopup::onPickGradient(CCObject*) {
    auto cb = m_onPickGradient;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileBgPickerPopup::onPickVideoAudio(CCObject*) {
    auto cb = m_onPickVideoAudio;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileBgPickerPopup::onPickReset(CCObject*) {
    auto cb = m_onPickReset;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileBgPickerPopup::onInfo(CCObject*) {
    PopupManager::get().alert(Localization::get().getString("profilebg.picker.info_title"), Localization::get().getString("profilebg.picker.info_body"), Localization::get().getString("profilesettings.info_ok")).showInstant();
}
