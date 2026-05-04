#include "ProfileSettingsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

ProfileSettingsPopup* ProfileSettingsPopup::create(int accountID) {
    auto ret = new ProfileSettingsPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileSettingsPopup::init(int accountID) {
    if (!Popup::init(340.f, 180.f)) return false;

    m_accountID = accountID;

    this->setTitle(Localization::get().getString("profilesettings.title").c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float cy = content.height / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    // Grid 2x2 para 4 botones
    float colSpacing = 70.f;
    float rowSpacing = 45.f;
    float topRowY = cy + 8.f;
    float botRowY = cy - rowSpacing + 8.f;
    float labelOffsetY = -24.f;

    // Boton de musica
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(32.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileSettingsPopup::onConfigureMusic));
        btn->setPosition({cx - colSpacing, topRowY});
        btn->setID("music-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.music_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx - colSpacing, topRowY + labelOffsetY});
        m_mainLayer->addChild(label);
    }

    // Boton de badge
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_collectible_goldKey_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(32.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileSettingsPopup::onConfigureBadge));
        btn->setPosition({cx + colSpacing, topRowY});
        btn->setID("badge-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.badge_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx + colSpacing, topRowY + labelOffsetY});
        m_mainLayer->addChild(label);
    }

    // Boton de imagen
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_duplicateBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_editBtn_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(32.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileSettingsPopup::onAddProfileImg));
        btn->setPosition({cx - colSpacing, botRowY});
        btn->setID("image-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.image_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx - colSpacing, botRowY + labelOffsetY});
        m_mainLayer->addChild(label);
    }

    // Boton de Comment BG (deshabilitado - saldra en 1.1.0)
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_paintBtn_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(32.f / maxDim);
        spr->setColor({120, 120, 120});

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileSettingsPopup::onConfigureCommentBgSoon));
        btn->setPosition({cx + colSpacing, botRowY});
        btn->setID("commentbg-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.comment_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setColor({120, 120, 120});
        label->setPosition({cx + colSpacing, botRowY + labelOffsetY});
        m_mainLayer->addChild(label);
    }

    // Boton de info
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(20.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileSettingsPopup::onInfo));
        btn->setPosition({content.width - 16.f, content.height - 16.f});
        btn->setID("info-button"_spr);
        menu->addChild(btn);
    }

    this->setZOrder(10500);
    this->setID("profile-settings-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

void ProfileSettingsPopup::onConfigureMusic(CCObject*) {
    auto cb = m_onMusicCallback;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileSettingsPopup::onConfigureBadge(CCObject*) {
    auto cb = m_onBadgeCallback;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileSettingsPopup::onAddProfileImg(CCObject*) {
    auto cb = m_onImageCallback;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileSettingsPopup::onConfigureCommentBg(CCObject*) {
    auto cb = m_onCommentBgCallback;
    this->onClose(nullptr);
    if (cb) cb();
}

void ProfileSettingsPopup::onConfigureCommentBgSoon(CCObject*) {
    FLAlertLayer::create(
        Localization::get().getString("profilesettings.comment_soon_title").c_str(),
        Localization::get().getString("profilesettings.comment_soon_body"),
        Localization::get().getString("profilesettings.info_ok").c_str()
    )->show();
}

void ProfileSettingsPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        Localization::get().getString("profilesettings.info_title").c_str(),
        Localization::get().getString("profilesettings.info_body"),
        Localization::get().getString("profilesettings.info_ok").c_str()
    )->show();
}
