#pragma once
#include <Geode/Geode.hpp>

namespace PaimonNotify {

    // Passthrough to Geode's standard Notification API.
    // Uses standard Geode icons (Success, Error, Warning, Info, None).
    inline geode::Notification* create(
        geode::ZStringView text,
        geode::NotificationIcon icon = geode::NotificationIcon::None,
        float time = geode::NOTIFICATION_DEFAULT_TIME
    ) {
        return geode::Notification::create(text, icon, time);
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
