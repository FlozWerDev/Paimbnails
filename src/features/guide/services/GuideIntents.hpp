#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GuideIntents.hpp
//
// Estructuras de datos para los "intents" (intenciones) que entiende la
// guia de Paimon. Cada intent agrupa:
//   - un id estable (para logging)
//   - un mapa de keywords por idioma (basta con que CUALQUIERA matchee)
//   - una respuesta por idioma (con fallback a ingles)
//   - una accion opcional que se ejecuta al confirmar la respuesta
//   - una animacion que Paimon hace al responder
//
// El servicio PaimonGuideService mantiene una lista de estos intents
// ordenados y devuelve el de mejor score frente a una query.
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class PaimonGuideChatPopup; // forward decl

// Animaciones que Paimon puede ejecutar al responder. Se mapean directamente
// a AnimatedPaimon::Animation en el paso 2; aqui se declara un enum-mirror
// para no acoplar el motor de intents al nodo grafico todavia.
enum class GuideAnimation {
    Talk,       // por defecto
    Surprise,   // exclamacion ("oh!")
    Point,      // apuntar (tipico cuando la accion lleva al usuario a otra UI)
    Wave,       // saludar (bienvenida)
    Sleep,      // baja atencion (fallback "no entendi")
};

// Tipo de intent. Los Functional son los que abren popups o explican settings
// (cursor, fondos, ...). Los Conversational son charla (saludo, halago,
// chiste). El matcher aplica thresholds distintos a cada tipo: los
// conversacionales tienen que ser MUY claros para matchear, los functional
// se aceptan con menos certeza porque siempre es util responder con un tip
// de configuracion.
enum class IntentKind {
    Functional,
    Conversational,
};

struct GuideIntent {
    std::string id;
    IntentKind kind = IntentKind::Functional;

    // Map idioma -> sinonimos (lowercase, ASCII sin acentos).
    // Si CUALQUIERA hace match en la query normalizada, el intent puntua.
    std::unordered_map<std::string, std::vector<std::string>> keywordsByLang;

    // Respuesta principal por idioma. La clave del map es el id de
    // Localization ("spanish", "english", ...). Soporta tags GD <cy>...</c>.
    std::unordered_map<std::string, std::string> responseByLang;

    // Variantes de respuesta para cuando el usuario repite la misma intencion.
    // Si esta vacio, se reusa la respuesta principal con un prefijo "Ya te dije...".
    // Indexado por idioma -> lista de variantes (sin la principal).
    std::unordered_map<std::string, std::vector<std::string>> variantsByLang;

    // Mensaje de "follow-up" para cuando el usuario hace una pregunta corta
    // tras este intent ("y como?", "more?"). Indexado por idioma. Si vacio
    // se reusa la respuesta principal.
    std::unordered_map<std::string, std::string> followUpByLang;

    // Score base del intent. Se usa para desempates cuando dos intents
    // matchean con la misma cantidad de keywords.
    int priority = 50;

    // ── Paigorit V1 (mejorado) ─────────────────────────────────────────
    //
    // PESO de la palabra clave principal de este intent. La idea es que
    // cuando dos intents matchean en la misma query (ej. "profile background"
    // matchea tanto el intent "profile" como el "background"), el ganador
    // sera aquel cuya keyword tenga mayor weight.
    //
    // Asi el usuario / desarrollador puede explicitar la jerarquia
    // semantica: "profile" es mas especifico que "background" porque
    // representa una entidad concreta del usuario (su perfil) y no un
    // simple atributo visual.
    //
    // Rangos sugeridos (1-200):
    //    50  - keyword generica (background, music, transitions)
    //    80  - keyword especifica de feature (cursor, pet, emote)
    //   100  - keyword central de identidad (profile, perfil)
    //   150  - keyword unica e inconfundible (paimon, paimbnails)
    //
    // Por defecto 50 — equivalente a priority del matcher anterior.
    int weight = 50;
    // ───────────────────────────────────────────────────────────────────

    // Lambda opcional que se ejecuta al pulsar "Llevame ahi" en el chat.
    // Recibe el popup actual para que pueda cerrarse o transitar.
    std::function<void(PaimonGuideChatPopup* popup)> action = nullptr;

    GuideAnimation animation = GuideAnimation::Talk;
};

struct GuideAnswer {
    std::string message;            // ya traducido al idioma activo
    std::function<void(PaimonGuideChatPopup* popup)> action;
    GuideAnimation animation = GuideAnimation::Talk;
    bool found = true;              // false => respuesta fallback generica
    std::string matchedIntentId;    // util para logs
};

} // namespace paimon::guide
