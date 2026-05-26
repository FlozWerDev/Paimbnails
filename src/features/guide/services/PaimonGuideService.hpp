#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <utility>

#include "GuideIntents.hpp"
#include "ConversationMemory.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// PaimonGuideService.hpp
//
// Singleton que conoce todos los GuideIntent y resuelve preguntas del usuario
// de forma 100% local (sin red, sin IA externa).
//
// Internamente mantiene una ConversationMemory para que las respuestas sean
// contextuales: detecta repeticiones, follow-ups cortos, y evita responder
// dos veces lo mismo seguido.
//
// Uso desde el chat popup:
//
//     auto answer = paimon::guide::PaimonGuideService::get()
//                       .ask("donde configuro el cursor?");
//     m_responseLabel->setString(answer.message.c_str());
//     if (answer.action) {
//         m_takeMeButton->setVisible(true);
//         m_pendingAction = answer.action;
//     }
//
// El servicio tambien expone:
//   - getSuggestions(): chips traducidos para el chat
//   - isEnabled() / setEnabled(): wrappers sobre el saved value
//                                 "guide-enabled" del Mod
//   - resetMemory(): limpia la conversacion (al cerrar el popup, por ej)
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class PaimonGuideService {
public:
    static PaimonGuideService& get();

    // API principal: el popup llama esto al pulsar Enviar.
    // Si la query es vacia o no matchea ningun intent, devuelve un answer
    // fallback con found=false.
    GuideAnswer ask(std::string const& userQuery);

    // Devuelve hasta 6 sugerencias en el idioma activo. Cada par es:
    //   first  = texto a mostrar en el chip
    //   second = query a inyectar al chat al pulsar el chip
    std::vector<std::pair<std::string, std::string>> getSuggestions();

    // Saved-value "guide-enabled".
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Util para tests / debug interno.
    std::size_t intentCount() const { return m_intents.size(); }

    // Acceso a la memoria conversacional. El popup la limpia cuando se cierra.
    ConversationMemory& memory() { return m_memory; }
    void resetMemory() { m_memory.clear(); }

private:
    PaimonGuideService();
    void registerIntents();

    // Normaliza una cadena: lowercase + colapsa espacios + remueve acentos
    // basicos comunes en ES/PT/FR. No es un normalizador Unicode completo,
    // solo lo suficiente para matchear keywords.
    static std::string normalize(std::string s);

    // Tokeniza la cadena normalizada en palabras separadas por whitespace
    // y signos de puntuacion ASCII basicos.
    static std::vector<std::string> tokenize(std::string const& normalized);

    // Construye un GuideAnswer "no entendi" en el idioma activo.
    GuideAnswer makeFallback() const;

    // Construye una respuesta para un intent concreto. Tiene en cuenta la
    // memoria para variar el mensaje cuando se repite (variantes).
    GuideAnswer buildAnswerFor(GuideIntent const& intent,
                               double matchScore,
                               std::string const& langId);

    // Construye una respuesta de "follow-up" reusando el ultimo intent
    // funcional. Llamado cuando looksLikeFollowUp(normalized) y existe
    // un last-topic.
    GuideAnswer buildFollowUpAnswer(GuideIntent const& intent,
                                    std::string const& langId);

    std::vector<GuideIntent> m_intents;
    ConversationMemory m_memory;
};

} // namespace paimon::guide
