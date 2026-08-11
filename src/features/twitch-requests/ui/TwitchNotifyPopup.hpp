#pragma once

// Popup del boton "Avisos": enciende el aviso de request nuevo y deja verlo en
// una pantalla de mentira (sitio, tamano y segundos) antes de salir al stream.

#include <Geode/Geode.hpp>

#include "../TwitchRequestNotify.hpp"

namespace paimon::twitch {

class TwitchNotifyPopup : public geode::Popup {
public:
    static TwitchNotifyPopup* create();

protected:
    bool init() override;

    void buildPreview(cocos2d::CCPoint origin, cocos2d::CCSize size);
    void buildOptions(cocos2d::CCPoint origin, cocos2d::CCSize size);

    // Vuelve a crear la tarjeta del previsualizador (cambio de contenido).
    void rebuildCard(bool replayEnter);
    // Solo la recoloca y refresca los textos (cambio de sitio o de medidas).
    void syncCard(bool replayEnter);
    void replayExit();
    cocos2d::CCPoint cardRestPoint() const;
    void apply(std::function<void(NotifyConfig&)> const& change, bool rebuild, bool replay);
    void onTest();

    NotifyConfig m_config;
    cocos2d::CCNode* m_screen = nullptr;
    cocos2d::CCNodeRGBA* m_card = nullptr;
    cocos2d::CCLabelBMFont* m_spotLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_animLabel = nullptr;
    // Cuanto mide la pantalla de mentira comparada con la de verdad.
    float m_ratio = 1.f;
    float m_infoWidth = 240.f;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::twitch
