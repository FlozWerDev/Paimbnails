#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// PaigoritV1.hpp (revision 2)
//
// "Paigorit V1" es el algoritmo de matching de Paimon. Esta segunda revision
// implementa el modelo "best-keyword-weight wins" pedido por el usuario:
//
//   "cada palabra tiene un valor y que tome la que tiene mayor valor"
//
// Es decir: en queries con varias palabras clave (ej. "profile background"),
// gana el INTENT cuya keyword matcheada tiene MAYOR PESO. No gana el que
// "cubre mas tokens" ni el que aparece al final de la frase, sino el que
// representa el concepto MAS especifico segun el peso definido por el
// desarrollador.
//
// Pesos sugeridos (campo GuideIntent::weight):
//
//     50  - generico (background, music, audio, layout, transitions)
//     80  - feature especifico (cursor, pet, emote, capture, discord)
//    100  - identidad del usuario (profile, perfil)
//    150  - palabra unica e inconfundible (paimon, paimbnails, hub)
//
// Asi:
//   - "profile background" -> profile (weight 100) > background (weight 50)
//   - "menu background"    -> background (weight 50) si menu no es intent
//   - "menu music"         -> menu-music (weight 80, compound)
//   - "profile music"      -> profile-music (weight 90, compound)
//
// El algoritmo combina:
//
//   1) Stopwords removal + sinonimos / stem (LightLemmatizer)
//   2) Por cada intent: para cada keyword, calcula score fuzzy con todas
//      las formas expandidas de la query. Si una keyword multi-palabra
//      aparece como compound exacto, recibe boost masivo.
//   3) De todos los matches del intent, toma el que tenga score >= MATCH_FLOOR
//      (default 70). Si el intent no tiene NINGUNA keyword con score >= floor,
//      el intent queda descalificado.
//   4) Para los intents calificados, el SCORE FINAL = intent.weight + bonus
//      por confianza del match (compound, exact, fuzzy_high).
//   5) Gana el intent con mayor finalScore.
//
// Detalles importantes:
//   - El score fuzzy ya NO es el que decide el ganador entre dos intents
//     ambos calificados. Solo decide si el intent SE CALIFICA. Una vez
//     calificado, lo que decide es el WEIGHT del intent.
//   - Esto resuelve el caso "profile background": ambos intents se
//     califican (las dos palabras matchean keywords con score 100), pero
//     gana el de mayor weight.
//   - Para queries donde solo UN intent califica, sigue funcionando como
//     antes: ese intent gana con su finalScore (weight + bonus).
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

struct ScoredIntent {
    GuideIntent const* intent = nullptr;

    // Mejor score fuzzy entre todas las keywords del intent vs query.
    double bestKeywordFuzzy = 0.0;

    // True si alguna keyword multi-palabra aparece como subsecuencia
    // contigua en la query (match exacto multi-palabra).
    bool hasCompoundMatch = false;

    // True si alguna keyword del intent es exactamente igual a algun
    // token de la query (post-stopwords).
    bool hasExactTokenMatch = false;

    // Confianza del match (0-30): combina las flags y el fuzzy score.
    double confidenceBonus = 0.0;

    // SCORE FINAL: weight del intent + confidenceBonus.
    double finalScore = 0.0;

    // True si el intent paso el threshold MATCH_FLOOR. Si false el intent
    // NO se considera calificado y no entra al ranking final.
    bool qualified = false;
};

struct PaigoritResult {
    GuideIntent const* best = nullptr;
    double bestScore = 0.0;
    double bestRawFuzzy = 0.0;
    bool ambiguous = false;
    std::vector<ScoredIntent> ranking; // ya filtrado a calificados, ordenado
};

class PaigoritV1 {
public:
    // Score fuzzy minimo para que una keyword se considere "matcheada"
    // contra la query. Si NINGUNA keyword del intent alcanza este floor,
    // el intent se descalifica.
    static constexpr double kMatchFloor = 70.0;

    // Floor mas alto para intents conversacionales (saludo, halago, etc).
    // Estos requieren matches mas precisos para evitar falsos positivos
    // en queries tecnicas.
    static constexpr double kMatchFloorConversational = 85.0;

    // En queries de >=4 palabras (post-stopwords) los conversacionales
    // requieren casi-perfecto.
    static constexpr double kMatchFloorConversationalLong = 92.0;

    // Si los dos mejores finalScore difieren menos de esto, marcamos
    // ambiguo (para logging / posible UI futura).
    static constexpr double kAmbiguityGap = 5.0;

    static PaigoritResult run(std::vector<GuideIntent> const& intents,
                              std::string const& normalizedQuery,
                              std::vector<std::string> const& queryTokens,
                              std::string const& langId);

private:
    // Score fuzzy entre la query (en sus formas expandidas) y una keyword.
    // Devuelve el maximo score posible (0-100).
    static double bestFuzzyAgainstKeyword(std::string const& normalizedQuery,
                                          std::vector<std::string> const& expandedTokens,
                                          std::string const& keyword);

    // True si la keyword multi-palabra aparece como subsecuencia contigua
    // de los tokens (despues de sinonimo / stem).
    static bool keywordAppearsAsCompound(std::vector<std::vector<std::string>> const& tokenForms,
                                         std::vector<std::string> const& kwTokens);

    // True si alguna forma del token matchea exacto a la keyword.
    static bool anyTokenFormEquals(std::vector<std::vector<std::string>> const& tokenForms,
                                   std::string const& keyword);
};

} // namespace paimon::guide
