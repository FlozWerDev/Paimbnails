#pragma once
#include <Geode/Geode.hpp>

namespace PaimonNotify {

    // Crea un SimplePlayer con los colores e icono del jugador actual,
    // escalado para caber como icono de notificacion (~20px).
    inline cocos2d::CCNode* createPlayerIcon() {
        auto* gm = GameManager::sharedState();
        if (!gm) return nullptr;

        int iconID = gm->m_playerFrame;
        auto* player = SimplePlayer::create(iconID);
        if (!player) return nullptr;

        // colores del jugador
        auto col1 = gm->colorForIdx(gm->m_playerColor);
        auto col2 = gm->colorForIdx(gm->m_playerColor2);
        player->setColor(col1);
        player->setSecondColor(col2);

        // glow
        if (gm->m_playerGlow) {
            auto glowCol = gm->colorForIdx(gm->m_playerGlowColor);
            player->setGlowOutline(glowCol);
        } else {
            player->disableGlowOutline();
        }

        // escalo a ~20px
        float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
        if (maxDim > 0) player->setScale(20.f / maxDim);

        return player;
    }

    // Crea una Notification con el icono del jugador para Success/Info.
    // Error y Warning mantienen los iconos estandar de Geode para claridad visual.
    inline geode::Notification* create(
        geode::ZStringView text,
        geode::NotificationIcon icon = geode::NotificationIcon::None,
        float time = geode::NOTIFICATION_DEFAULT_TIME
    ) {
        using namespace geode;

        // para success e info, intento usar el icono del jugador
        if (icon == NotificationIcon::Success || icon == NotificationIcon::Info) {
            auto* playerIcon = createPlayerIcon();
            if (playerIcon) {
                return Notification::create(text, playerIcon, time);
            }
        }

        // fallback: icono estandar
        return Notification::create(text, icon, time);
    }

    // Atajo: crear y mostrar directamente
    inline void show(
        geode::ZStringView text,
        geode::NotificationIcon icon = geode::NotificationIcon::None,
        float time = geode::NOTIFICATION_DEFAULT_TIME
    ) {
        if (auto* notif = create(text, icon, time)) {
            notif->show();
        }
    }

} // namespace PaimonNotify
