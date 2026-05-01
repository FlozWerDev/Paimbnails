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

    bool init(int accountID);

    void onConfigureMusic(cocos2d::CCObject*);
    void onAddProfileImg(cocos2d::CCObject*);
    void onConfigureBadge(cocos2d::CCObject*);
    void onConfigureCommentBg(cocos2d::CCObject*);
    void onConfigureCommentBgSoon(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);

public:
    static ProfileSettingsPopup* create(int accountID);

    void setOnMusicCallback(geode::CopyableFunction<void()> cb) { m_onMusicCallback = std::move(cb); }
    void setOnImageCallback(geode::CopyableFunction<void()> cb) { m_onImageCallback = std::move(cb); }
    void setOnBadgeCallback(geode::CopyableFunction<void()> cb) { m_onBadgeCallback = std::move(cb); }
    void setOnCommentBgCallback(geode::CopyableFunction<void()> cb) { m_onCommentBgCallback = std::move(cb); }
};