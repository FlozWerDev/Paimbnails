#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>

// ProfileBgPickerPopup
// Chooser que ofrece varias formas de configurar el fondo de perfil:
//   1. Subir media (imagen / GIF / video) — flujo existente.
//   2. Degradado basado en los colores de los iconos del jugador.
//   3. Audio del video — usa el audio del video del fondo como musica de
//      perfil y borra cualquier musica configurada.
//   4. Restablecer al fondo por defecto.
//
// El popup no realiza la accion por si mismo: dispara callbacks para que
// ProfilePage controle la logica de subida / configuracion / borrado.
class ProfileBgPickerPopup : public geode::Popup {
public:
    using SimpleCallback = geode::CopyableFunction<void()>;

    static ProfileBgPickerPopup* create(int accountID);

    void setOnPickMedia(SimpleCallback cb)      { m_onPickMedia      = std::move(cb); }
    void setOnPickGradient(SimpleCallback cb)   { m_onPickGradient   = std::move(cb); }
    void setOnPickVideoAudio(SimpleCallback cb) { m_onPickVideoAudio = std::move(cb); }
    void setOnPickReset(SimpleCallback cb)      { m_onPickReset      = std::move(cb); }

protected:
    int m_accountID = 0;
    SimpleCallback m_onPickMedia;
    SimpleCallback m_onPickGradient;
    SimpleCallback m_onPickVideoAudio;
    SimpleCallback m_onPickReset;

    bool init(int accountID);

    void onPickMedia(cocos2d::CCObject*);
    void onPickGradient(cocos2d::CCObject*);
    void onPickVideoAudio(cocos2d::CCObject*);
    void onPickReset(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);
};
