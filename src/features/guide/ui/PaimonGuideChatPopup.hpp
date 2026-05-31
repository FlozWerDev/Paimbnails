#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <functional>
#include <string>

#include "AnimatedPaimon.hpp"
#include "AnimatedTextInput.hpp"
#include "../services/GuideIntents.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// PaimonGuideChatPopup
//
// Popup del chat con Paimon.
//
//   - AnimatedPaimon a la izquierda (saluda al abrir, habla al responder).
//   - Panel de mensaje (typewriter) arriba.
//   - AnimatedTextInput abajo con boton "Preguntar" a la derecha.
//   - Boton "Llevame ahi" se hace visible cuando la respuesta tiene action.
//
// Sigue el patron del proyecto: hereda de geode::Popup sin template, override
// init() que llama Popup::init(width, height).
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class PaimonGuideChatPopup : public geode::Popup {
public:
    static PaimonGuideChatPopup* create();

    // Inyecta una pregunta como si la hubiera escrito el usuario.
    void submitQuery(std::string const& query);

protected:
    bool init() override;

    void onSubmitButton(cocos2d::CCObject* sender);
    void onTakeMeThere(cocos2d::CCObject* sender);
    void onSuggestionChip(cocos2d::CCObject* sender);

    // Reemplaza el mensaje actual con uno nuevo (con efecto typewriter).
    void displayMessage(std::string const& message);
    void onTypewriterTick(float dt);

    AnimatedPaimon* m_paimon = nullptr;
    AnimatedTextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_responseBg = nullptr;
    cocos2d::CCLabelBMFont* m_responseLabel = nullptr;
    CCMenuItemSpriteExtra* m_takeMeBtn = nullptr;
    cocos2d::CCMenu* m_takeMeMenu = nullptr;
    cocos2d::CCMenu* m_suggestionsMenu = nullptr;

    // Estado del typewriter
    std::string m_pendingMessage;
    std::size_t m_typewriterIndex = 0;

    // Accion pendiente del ultimo intent (para "Llevame ahi")
    std::function<void(PaimonGuideChatPopup*)> m_pendingAction;
};

} // namespace paimon::guide
