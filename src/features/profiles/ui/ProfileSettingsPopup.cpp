#include "ProfileSettingsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../global-icon/services/GlobalIconService.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include <Geode/binding/GameManager.hpp>

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
        btn->setPosition({cx, topRowY});
        btn->setID("badge-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.badge_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx, topRowY + labelOffsetY});
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
        btn->setPosition({cx + colSpacing, topRowY});
        btn->setID("image-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.image_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx + colSpacing, topRowY + labelOffsetY});
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
        btn->setPosition({cx - 45.f, botRowY});
        btn->setID("commentbg-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.comment_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setColor({120, 120, 120});
        label->setPosition({cx - 45.f, botRowY + labelOffsetY});
        m_mainLayer->addChild(label);
    }

    // Boton de Global Icon (toggle): usa el cubo del jugador como icono.
    // Gris cuando esta desactivado, a color cuando esta activo.
    {
        m_globalIconEnabled = paimon::globalicon::GlobalIconService::isEnabledLocally();

        auto gm = GameManager::sharedState();
        if (gm) {
            m_globalIconColor1 = gm->colorForIdx(gm->getPlayerColor());
            m_globalIconColor2 = gm->colorForIdx(gm->getPlayerColor2());
        }

        auto iconSpr = SimplePlayer::create(0);
        if (iconSpr) {
            iconSpr->updatePlayerFrame(gm ? gm->getPlayerFrame() : 1, IconType::Cube);
            float maxDim = std::max(iconSpr->getContentWidth(), iconSpr->getContentHeight());
            if (maxDim > 0) iconSpr->setScale(28.f / maxDim);
        }
        m_globalIconSprite = iconSpr;

        // Un SimplePlayer puede quedar con contentSize 0, lo que deja al
        // CCMenuItemSpriteExtra sin area de toque. Usamos un fondo invisible
        // con tamaño fijo como hitbox y montamos el icono encima.
        auto hit = CCLayerColor::create(ccc4(0, 0, 0, 0), 34.f, 34.f);
        hit->ignoreAnchorPointForPosition(false);
        hit->setAnchorPoint({0.5f, 0.5f});

        auto btn = CCMenuItemSpriteExtra::create(hit, this, menu_selector(ProfileSettingsPopup::onToggleGlobalIcon));
        btn->setContentSize({34.f, 34.f});
        if (iconSpr) {
            iconSpr->setPosition({17.f, 17.f}); // centro del hitbox 34x34
            btn->addChild(iconSpr);
        }
        btn->setPosition({cx + 45.f, botRowY});
        btn->setID("globalicon-button"_spr);
        menu->addChild(btn);

        auto label = CCLabelBMFont::create(Localization::get().getString("profilesettings.globalicon_label").c_str(), "bigFont.fnt");
        label->setScale(0.30f);
        label->setPosition({cx + 45.f, botRowY + labelOffsetY});
        m_mainLayer->addChild(label);

        applyGlobalIconColor();
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

void ProfileSettingsPopup::applyGlobalIconColor() {
    if (!m_globalIconSprite) return;
    if (m_globalIconEnabled) {
        m_globalIconSprite->setColor(m_globalIconColor1);
        m_globalIconSprite->setSecondColor(m_globalIconColor2);
    } else {
        // Oscuro/gris cuando esta desactivado.
        m_globalIconSprite->setColor({120, 120, 120});
        m_globalIconSprite->setSecondColor({90, 90, 90});
    }
}

void ProfileSettingsPopup::onToggleGlobalIcon(CCObject*) {
    if (!paimon::compat::ModCompat::isMoreIconsLoaded()) {
        FLAlertLayer::create(
            Localization::get().getString("globalicon.requires_moreicons_title").c_str(),
            Localization::get().getString("globalicon.requires_moreicons_body"),
            Localization::get().getString("profilesettings.info_ok").c_str()
        )->show();
        return;
    }

    // Flip optimista del estado visual; el resultado real (y la persistencia
    // del flag) lo maneja el callback en ProfilePage segun la respuesta del
    // servidor.
    m_globalIconEnabled = !m_globalIconEnabled;
    applyGlobalIconColor();

    auto cb = m_onGlobalIconCallback;
    bool desired = m_globalIconEnabled;
    if (cb) cb(desired);
}

void ProfileSettingsPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        Localization::get().getString("profilesettings.info_title").c_str(),
        Localization::get().getString("profilesettings.info_body"),
        Localization::get().getString("profilesettings.info_ok").c_str()
    )->show();
}
