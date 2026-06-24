#pragma once
#include <Geode/Geode.hpp>

// Configuracion del perfil: musica, imagen, badge y fondo de comentarios
class ProfileSettingsPopup : public geode::Popup {
protected:
    int m_accountID = 0;
    geode::CopyableFunction<void()> m_onMusicCallback;
    geode::CopyableFunction<void()> m_onImageCallback;
    geode::CopyableFunction<void()> m_onBadgeCallback;
    geode::CopyableFunction<void()> m_onCommentBgCallback;
    geode::CopyableFunction<void(bool enabled)> m_onGlobalIconCallback;

    // Estado del toggle Global Icon (icono gris cuando off, color cuando on).
    geode::Ref<SimplePlayer> m_globalIconSprite;
    cocos2d::ccColor3B m_globalIconColor1 = {255, 255, 255};
    cocos2d::ccColor3B m_globalIconColor2 = {255, 255, 255};
    bool m_globalIconEnabled = false;

    bool init(int accountID);

    void onConfigureMusic(cocos2d::CCObject*);
    void onAddProfileImg(cocos2d::CCObject*);
    void onConfigureBadge(cocos2d::CCObject*);
    void onConfigureCommentBg(cocos2d::CCObject*);
    void onConfigureCommentBgSoon(cocos2d::CCObject*);
    void onToggleGlobalIcon(cocos2d::CCObject*);
    void applyGlobalIconColor();
    void onInfo(cocos2d::CCObject*);

public:
    static ProfileSettingsPopup* create(int accountID);

    void setOnMusicCallback(geode::CopyableFunction<void()> cb) { m_onMusicCallback = std::move(cb); }
    void setOnImageCallback(geode::CopyableFunction<void()> cb) { m_onImageCallback = std::move(cb); }
    void setOnBadgeCallback(geode::CopyableFunction<void()> cb) { m_onBadgeCallback = std::move(cb); }
    void setOnCommentBgCallback(geode::CopyableFunction<void()> cb) { m_onCommentBgCallback = std::move(cb); }
    // cb(enabled): enabled = nuevo estado deseado tras el toggle.
    void setOnGlobalIconCallback(geode::CopyableFunction<void(bool enabled)> cb) { m_onGlobalIconCallback = std::move(cb); }
};