#include "PaimonGuideService.hpp"
#include "PaigoritV1.hpp"
#include "PopupRegistry.hpp"

#include "../../../utils/Localization.hpp"
#include "../ui/PaimonGuideChatPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::guide {

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Helpers locales
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace {

std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}

// Mapa minimo de "letra acentuada -> letra plana" para normalizar queries
// del usuario. Cubre ES/PT/FR comunes (UTF-8 0xC3 0xXX).
std::string stripBasicAccents(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        unsigned char c = static_cast<unsigned char>(in[i]);

        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char c2 = static_cast<unsigned char>(in[i + 1]);
            char replacement = 0;
            switch (c2) {
                case 0xA1: case 0xA0: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
                    replacement = 'a'; break;
                case 0xA9: case 0xA8: case 0xAA: case 0xAB:
                    replacement = 'e'; break;
                case 0xAD: case 0xAC: case 0xAE: case 0xAF:
                    replacement = 'i'; break;
                case 0xB3: case 0xB2: case 0xB4: case 0xB5: case 0xB6:
                    replacement = 'o'; break;
                case 0xBA: case 0xB9: case 0xBB: case 0xBC:
                    replacement = 'u'; break;
                case 0xB1:
                    replacement = 'n'; break; // Ã±
                case 0x81: case 0x80: case 0x82: case 0x83: case 0x84: case 0x85:
                    replacement = 'a'; break;
                case 0x89: case 0x88: case 0x8A: case 0x8B:
                    replacement = 'e'; break;
                case 0x8D: case 0x8C: case 0x8E: case 0x8F:
                    replacement = 'i'; break;
                case 0x93: case 0x92: case 0x94: case 0x95: case 0x96:
                    replacement = 'o'; break;
                case 0x9A: case 0x99: case 0x9B: case 0x9C:
                    replacement = 'u'; break;
                case 0x91:
                    replacement = 'n'; break; // Ã‘
                default: break;
            }
            if (replacement != 0) {
                out.push_back(replacement);
                i += 2;
                continue;
            }
        }

        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

// Helper de accion: muestra un popup cuyo create() es global/sin namespace
// y sin parametros. Capturado por las lambdas de los intents conversacionales
// que en un futuro pudieran querer abrir popups (hoy no lo necesitan,
// PopupRegistry.cpp tiene su propia copia para los entries del registro).
template <typename PopupT>
void openSimplePopup() {
    if (auto* popup = PopupT::create()) {
        popup->show();
    }
}

} // namespace

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// API
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

PaimonGuideService& PaimonGuideService::get() {
    static PaimonGuideService instance;
    return instance;
}

PaimonGuideService::PaimonGuideService() {
    registerIntents();
}

bool PaimonGuideService::isEnabled() const {
    auto* mod = geode::Mod::get();
    if (!mod) return false;
    return mod->getSavedValue<bool>("guide-enabled", false);
}

void PaimonGuideService::setEnabled(bool enabled) {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    mod->setSavedValue<bool>("guide-enabled", enabled);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Normalizacion / tokenizacion
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

std::string PaimonGuideService::normalize(std::string s) {
    s = stripBasicAccents(s);
    std::string out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) {
            char low = static_cast<char>(std::tolower(u));
            if (std::isalnum(static_cast<unsigned char>(low))) {
                out.push_back(low);
                lastSpace = false;
            } else {
                if (!lastSpace) {
                    out.push_back(' ');
                    lastSpace = true;
                }
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> PaimonGuideService::tokenize(std::string const& normalized) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : normalized) {
        if (c == ' ') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Set completo de intents
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void PaimonGuideService::registerIntents() {
    // 1) Cargar todos los popups del registro como intents.
    //    El registry es la fuente de verdad: cada popup expone su nombre
    //    real (displayName), sus aliases, y una accion para abrirse.
    //    Asi cuando el usuario escribe "profile background", Paigorit
    //    matchea contra el displayName "Profile Background" del entry
    //    y abre ProfileBgPickerPopup automaticamente.
    auto& registry = PopupRegistry::get();
    for (auto const& entry : registry.entries()) {
        m_intents.push_back(PopupRegistry::toIntent(entry));
    }

    // 2) Intents conversacionales (no provienen de popups).
    //    Tienen weight bajo para no robar matches a las preguntas tecnicas.

    // === Help / general ==========================================
    {
        GuideIntent it;
        it.id = "help-general";
        it.kind = IntentKind::Conversational;
        it.priority = 25;
        it.weight = 40;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "help", "guide", "tutorial", "what can you do", "options"
        };
        it.keywordsByLang["spanish"] = {
            "ayuda", "guia", "tutorial", "que puedes hacer", "opciones"
        };
        it.responseByLang["english"] =
            "I learn from the popups in this mod. Try asking for "
            "<cy>profile background</c>, <cy>menu music</c>, <cy>cursor</c>, "
            "<cy>discord</c>, <cy>quick hub</c>, <cy>thumbnails</c>...";
        it.responseByLang["spanish"] =
            "Yo aprendo de los popups de este mod. Pruebame con "
            "<cy>fondo de perfil</c>, <cy>musica del menu</c>, <cy>cursor</c>, "
            "<cy>discord</c>, <cy>quick hub</c>, <cy>miniaturas</c>...";
        m_intents.push_back(std::move(it));
    }

    // === Who are you ============================================
    {
        GuideIntent it;
        it.id = "who-are-you";
        it.kind = IntentKind::Conversational;
        it.priority = 30;
        it.weight = 35;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "who are you", "what are you", "who is paimon", "your name"
        };
        it.keywordsByLang["spanish"] = {
            "quien eres", "que eres", "como te llamas", "quien es paimon"
        };
        it.responseByLang["english"] =
            "I'm <cy>Paimon</c>! Your tiny floating guide for Paimbnails. "
            "Ask me where to configure things and I'll take you there!";
        it.responseByLang["spanish"] =
            "Soy <cy>Paimon</c>, tu pequena guia de Paimbnails. "
            "Preguntame donde configurar las cosas y te llevo!";
        m_intents.push_back(std::move(it));
    }

    // === Thanks =================================================
    {
        GuideIntent it;
        it.id = "thanks";
        it.kind = IntentKind::Conversational;
        it.priority = 20;
        it.weight = 30;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "thanks", "thank you", "ty", "thx", "appreciate"
        };
        it.keywordsByLang["spanish"] = {
            "gracias", "muchas gracias", "thank you", "te agradezco"
        };
        it.responseByLang["english"] = "You're welcome! <cg>Anything else?</c>";
        it.responseByLang["spanish"] = "De nada! <cg>Algo mas?</c>";
        m_intents.push_back(std::move(it));
    }

    // === Greetings ==============================================
    {
        GuideIntent it;
        it.id = "greeting";
        it.kind = IntentKind::Conversational;
        it.priority = 15;
        it.weight = 30;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "hi", "hello", "hey", "good morning", "good evening",
            "good afternoon", "yo", "sup"
        };
        it.keywordsByLang["spanish"] = {
            "hola", "buenas", "buenos dias", "buenas tardes", "buenas noches",
            "ey", "que tal", "saludos"
        };
        it.responseByLang["english"] =
            "Hi there! <cg>I'm Paimon</c>. Ask me about any popup or layer!";
        it.responseByLang["spanish"] =
            "Hola! <cg>Soy Paimon</c>. Preguntame por cualquier popup o layer!";
        it.variantsByLang["english"] = {
            "Hello again! What do you want to find this time?",
            "Hi! Same Paimon, ready to help.",
        };
        it.variantsByLang["spanish"] = {
            "Hola otra vez! Que vamos a buscar ahora?",
            "Hola! Soy la misma Paimon, lista para ayudarte.",
        };
        m_intents.push_back(std::move(it));
    }

    // === How are you ============================================
    {
        GuideIntent it;
        it.id = "how-are-you";
        it.kind = IntentKind::Conversational;
        it.priority = 15;
        it.weight = 30;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "how are you", "are you ok", "how do you do", "you fine"
        };
        it.keywordsByLang["spanish"] = {
            "como estas", "que tal estas", "como te va", "estas bien", "como andas"
        };
        it.responseByLang["english"] =
            "I'm great! Floating around, ready to help. <cg>You?</c>";
        it.responseByLang["spanish"] =
            "Genial! Flotando por aqui, lista para ayudarte. <cg>Y tu?</c>";
        m_intents.push_back(std::move(it));
    }

    // === Compliments ============================================
    {
        GuideIntent it;
        it.id = "compliment";
        it.kind = IntentKind::Conversational;
        it.priority = 15;
        it.weight = 30;
        it.animation = GuideAnimation::Surprise;
        it.keywordsByLang["english"] = {
            "you are great", "you are awesome", "you are cool", "i love you",
            "best", "amazing", "wonderful"
        };
        it.keywordsByLang["spanish"] = {
            "eres genial", "eres la mejor", "te amo", "te quiero",
            "que linda", "que bonita", "increible"
        };
        it.responseByLang["english"] =
            "Aww, you're sweet! <cy>Paimon happy~</c>";
        it.responseByLang["spanish"] =
            "Aww, que tierno! <cy>Paimon feliz~</c>";
        m_intents.push_back(std::move(it));
    }

    // === Goodbyes ===============================================
    {
        GuideIntent it;
        it.id = "goodbye";
        it.kind = IntentKind::Conversational;
        it.priority = 15;
        it.weight = 30;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "bye", "goodbye", "see you", "see ya", "later", "cya"
        };
        it.keywordsByLang["spanish"] = {
            "adios", "chao", "nos vemos", "hasta luego", "hasta pronto", "bye"
        };
        it.responseByLang["english"] =
            "Bye! Come back if you need help <cy>~</c>";
        it.responseByLang["spanish"] =
            "Adios! Vuelve cuando necesites algo <cy>~</c>";
        m_intents.push_back(std::move(it));
    }

    // === Joke ===================================================
    {
        GuideIntent it;
        it.id = "joke";
        it.kind = IntentKind::Conversational;
        it.priority = 15;
        it.weight = 30;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "joke", "tell me a joke", "make me laugh", "funny", "haha"
        };
        it.keywordsByLang["spanish"] = {
            "chiste", "cuentame un chiste", "hazme reir", "gracioso", "broma", "jaja"
        };
        it.responseByLang["english"] =
            "Why did the cube cross the road? <cg>To get to the demon side!</c>";
        it.responseByLang["spanish"] =
            "Por que cruzo el cubo la calle? <cg>Para llegar al lado demon!</c>";
        m_intents.push_back(std::move(it));
    }

    // === What can you do ========================================
    {
        GuideIntent it;
        it.id = "what-can-you-do";
        it.kind = IntentKind::Conversational;
        it.priority = 18;
        it.weight = 35;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "what can you do", "what do you do", "your features", "your capabilities"
        };
        it.keywordsByLang["spanish"] = {
            "que sabes hacer", "que puedes hacer", "tus funciones", "que opciones hay"
        };
        it.responseByLang["english"] =
            "I take you to any popup or layer of Paimbnails. Just say its "
            "name (or part of it): <cy>profile background</c>, <cy>menu music</c>, "
            "<cy>discord rich presence</c>, <cy>quick hub</c>, <cy>thumbnails</c>, "
            "and so on.";
        it.responseByLang["spanish"] =
            "Te llevo a cualquier popup o layer de Paimbnails. Solo dime su "
            "nombre (o parte): <cy>fondo de perfil</c>, <cy>musica del menu</c>, "
            "<cy>discord rich presence</c>, <cy>quick hub</c>, <cy>miniaturas</c>, "
            "etc.";
        m_intents.push_back(std::move(it));
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Matcher
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

GuideAnswer PaimonGuideService::makeFallback() const {
    GuideAnswer ans;
    ans.found = false;
    ans.animation = GuideAnimation::Sleep;
    ans.message = tr(
        "pai.guide.fallback",
        "Hmm, I don't know that one. Try keywords like cursor, music, "
        "background, discord, emotes, profile..."
    );
    return ans;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Helpers internos para construir respuestas
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace {

// Pickeo determinista de variante segun cuantas veces hemos contestado este
// intent en los ultimos 60s. Asi la primera vez decimos la respuesta
// principal, la segunda una variante, la tercera otra, etc.
std::string pickResponseString(GuideIntent const& intent,
                               std::string const& langId,
                               int repeatCount) {
    auto pickFromLang = [&](std::string const& lang) -> std::string {
        if (repeatCount <= 0) {
            auto it = intent.responseByLang.find(lang);
            if (it != intent.responseByLang.end()) return it->second;
            return {};
        }
        auto vIt = intent.variantsByLang.find(lang);
        if (vIt != intent.variantsByLang.end() && !vIt->second.empty()) {
            // Indice 0..N-1 para variantes (el primer repeat usa la 0).
            std::size_t idx = static_cast<std::size_t>(repeatCount - 1)
                              % vIt->second.size();
            return vIt->second[idx];
        }
        // Sin variantes: reusar respuesta principal con prefijo "ya te dije".
        auto rIt = intent.responseByLang.find(lang);
        if (rIt != intent.responseByLang.end()) return rIt->second;
        return {};
    };

    auto str = pickFromLang(langId);
    if (str.empty()) str = pickFromLang("english");
    return str;
}

} // namespace

GuideAnswer PaimonGuideService::buildAnswerFor(GuideIntent const& intent,
                                               double matchScore,
                                               std::string const& langId) {
    int repeats = m_memory.recentMatchesOf(intent.id);

    GuideAnswer ans;
    ans.found = true;
    ans.matchedIntentId = intent.id;
    ans.action = intent.action;
    ans.animation = intent.animation;
    ans.message = pickResponseString(intent, langId, repeats);

    // Si lo repitio y no hay variantes, anadir prefijo amistoso.
    if (repeats > 0) {
        auto vIt = intent.variantsByLang.find(langId);
        bool hasVariants = (vIt != intent.variantsByLang.end()
                            && !vIt->second.empty());
        if (!hasVariants) {
            std::string prefix = (langId == "spanish")
                ? "Como te dije: "
                : "As I said: ";
            ans.message = prefix + ans.message;
        }
    }

    (void)matchScore;
    return ans;
}

GuideAnswer PaimonGuideService::buildFollowUpAnswer(GuideIntent const& intent,
                                                    std::string const& langId) {
    GuideAnswer ans;
    ans.found = true;
    ans.matchedIntentId = intent.id;
    ans.action = intent.action;
    ans.animation = intent.animation;

    auto fuIt = intent.followUpByLang.find(langId);
    if (fuIt == intent.followUpByLang.end()) {
        fuIt = intent.followUpByLang.find("english");
    }

    if (fuIt != intent.followUpByLang.end() && !fuIt->second.empty()) {
        ans.message = fuIt->second;
    } else {
        // Sin follow-up definido: reusar respuesta principal con prefijo.
        auto rIt = intent.responseByLang.find(langId);
        if (rIt == intent.responseByLang.end()) {
            rIt = intent.responseByLang.find("english");
        }
        std::string prefix = (langId == "spanish")
            ? "Sobre eso mismo: "
            : "About that: ";
        ans.message = prefix + (rIt != intent.responseByLang.end()
                                ? rIt->second
                                : "...");
    }

    return ans;
}

GuideAnswer PaimonGuideService::ask(std::string const& userQuery) {
    auto langId = Localization::get().getCurrentLanguageId();
    auto normalized = normalize(userQuery);

    if (normalized.empty()) {
        return makeFallback();
    }

    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // 1) Detectar follow-up corto ("y como?", "more?", etc).
    //    Si la query parece un follow-up Y existe un last-topic funcional
    //    reciente, devolvemos la respuesta follow-up de ese intent en lugar
    //    de buscar de cero.
    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (ConversationMemory::looksLikeFollowUp(normalized)) {
        if (auto last = m_memory.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < ConversationMemory::kRecentSecs)
        {
            for (auto const& intent : m_intents) {
                if (intent.id == last->matchedIntentId) {
                    auto ans = buildFollowUpAnswer(intent, langId);
                    // Registrar el turno (con flag de follow-up implicit:
                    // matcheamos el mismo intent que el anterior).
                    ConversationTurn turn;
                    turn.userQuery = userQuery;
                    turn.normalizedQuery = normalized;
                    turn.matchedIntentId = intent.id;
                    turn.wasFunctional = (intent.kind == IntentKind::Functional);
                    turn.matchScore = 100.0; // forzado por contexto
                    m_memory.recordTurn(std::move(turn));
                    return ans;
                }
            }
        }
        // Si no hay last-topic, caemos al matcher normal.
    }

    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // 2) Matcher Paigorit V1.
    //
    //    El algoritmo combina varias senales (fuzzy + compound bonus +
    //    coverage + positional weight + IDF penalty) para decidir el
    //    intent ganador. Ver PaigoritV1.hpp para detalles.
    //
    //    Ventaja sobre el matcher anterior (rapidfuzz puro): resuelve
    //    correctamente queries compuestas como "background profile"
    //    favoreciendo el sustantivo principal (tail token).
    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    auto tokens = tokenize(normalized);
    auto paigorit = PaigoritV1::run(m_intents, normalized, tokens, langId);

    GuideIntent const* best = paigorit.best;
    double bestRaw = paigorit.bestRawFuzzy;

    // Log de Paigorit (top-3) para debug. Se puede quitar en release.
    if (!paigorit.ranking.empty()) {
        log::debug("Paigorit V1 query='{}' top results:", normalized);
        std::size_t logged = 0;
        for (auto const& s : paigorit.ranking) {
            if (logged >= 3) break;
            log::debug("  [{}] id={} weight={} fuzzy={:.1f} compound={} "
                       "exact={} confidence={:.1f} final={:.2f}",
                       logged, s.intent->id, s.intent->weight,
                       s.bestKeywordFuzzy,
                       s.hasCompoundMatch ? "yes" : "no",
                       s.hasExactTokenMatch ? "yes" : "no",
                       s.confidenceBonus, s.finalScore);
            ++logged;
        }
        if (paigorit.ambiguous) {
            log::debug("Paigorit V1: ambiguous result (gap < {})",
                       PaigoritV1::kAmbiguityGap);
        }
    }

    GuideAnswer ans;
    if (best) {
        ans = buildAnswerFor(*best, bestRaw, langId);
    } else {
        ans = makeFallback();
    }

    // Registrar turno en la memoria.
    ConversationTurn turn;
    turn.userQuery = userQuery;
    turn.normalizedQuery = normalized;
    turn.matchedIntentId = best ? best->id : "";
    turn.wasFunctional = best && (best->kind == IntentKind::Functional);
    turn.matchScore = bestRaw;
    m_memory.recordTurn(std::move(turn));

    return ans;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Sugerencias
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

std::vector<std::pair<std::string, std::string>>
PaimonGuideService::getSuggestions() {
    auto langId = Localization::get().getCurrentLanguageId();
    bool es = (langId == "spanish");

    // 6 chips representativos del set completo. Los textos del chip son
    // cortos para que entren en el RowLayout del popup; las queries son
    // mas explicitas para asegurar que matcheen los intents correctos.
    std::vector<std::pair<std::string, std::string>> result;
    if (es) {
        result.push_back({"cursor",   "donde configuro el cursor"});
        result.push_back({"musica",   "como pongo musica de menu"});
        result.push_back({"fondos",   "donde cambio los fondos"});
        result.push_back({"perfil",   "configurar mi perfil"});
        result.push_back({"emotes",   "como uso emotes"});
        result.push_back({"ayuda",    "ayuda general"});
    } else {
        result.push_back({"cursor",      "where do i configure cursor"});
        result.push_back({"music",       "how do i set menu music"});
        result.push_back({"backgrounds", "where do i change backgrounds"});
        result.push_back({"profile",     "configure my profile"});
        result.push_back({"emotes",      "how do i use emotes"});
        result.push_back({"help",        "general help"});
    }
    return result;
}

} // namespace paimon::guide
