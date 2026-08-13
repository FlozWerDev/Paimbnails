#include "PaimonGuideService.hpp"
#include "PaigoritV1.hpp"
#include "PopupRegistry.hpp"
#include "LightLemmatizer.hpp"

#include "../../../utils/Localization.hpp"
#include "../ui/PaimonGuideChatPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>
#include <cctype>
#include <optional>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}

// Minimal "accented -> plain letter" map to normalize user queries (common ES/PT/FR, UTF-8 0xC3 0xXX).
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
                    replacement = 'n'; break; // n with tilde (lower)
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
                    replacement = 'n'; break; // n with tilde (upper)
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

template <typename PopupT>
void openSimplePopup() {
    if (auto* popup = PopupT::create()) {
        popup->show();
    }
}

}

PaimonGuideService& PaimonGuideService::get() {
    static PaimonGuideService instance;
    return instance;
}

PaimonGuideService::PaimonGuideService() {
    registerIntents();
    m_engine.setTopics(buildTopicKnowledge());
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

bool PaimonGuideService::isMaxAvailable() const {
    return GeminiClient::available();
}

GuideMode PaimonGuideService::getMode() const {
    auto* mod = geode::Mod::get();
    if (!mod || !isMaxAvailable()) return GuideMode::Assistant;
    return mod->getSavedValue<bool>("guide-mode-max", false)
        ? GuideMode::Max
        : GuideMode::Assistant;
}

void PaimonGuideService::setMode(GuideMode mode) {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    mod->setSavedValue<bool>("guide-mode-max", mode == GuideMode::Max && isMaxAvailable());
}

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

void PaimonGuideService::registerIntents() {
    auto& registry = PopupRegistry::get();
    for (auto const& entry : registry.entries()) {
        m_intents.push_back(PopupRegistry::toIntent(entry));
    }


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
            "I know every feature of this mod. Try "
            "<cy>cursor</c>, <cy>menu music</c>, <cy>collab</c>, <cy>icons</c>, "
            "<cy>capture</c>, <cy>editor history</c>, or say a problem like "
            "<cy>thumbnails not loading</c>...";
        it.responseByLang["spanish"] =
            "Conozco todas las funciones del mod. Prueba "
            "<cy>cursor</c>, <cy>musica del menu</c>, <cy>collab</c>, <cy>iconos</c>, "
            "<cy>captura</c>, <cy>historial del editor</c>, o un problema como "
            "<cy>no se ven miniaturas</c>...";
        m_intents.push_back(std::move(it));
    }

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

    int featureCount = static_cast<int>(PopupRegistry::get().entries().size());
    std::string version = "?";
    if (auto* mod = geode::Mod::get()) {
        // toNonVString: las respuestas ya escriben la "v" delante.
        version = mod->getVersion().toNonVString(false);
    }

    {
        GuideIntent it;
        it.id = "mod-about";
        it.kind = IntentKind::Conversational;
        it.priority = 30;
        it.weight = 42;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "what is paimbnails", "about paimbnails", "about the mod",
            "what is this mod", "tell me about paimbnails", "what does this mod do",
            "paimbnails"
        };
        it.keywordsByLang["spanish"] = {
            "que es paimbnails", "sobre el mod", "de que trata",
            "que es este mod", "sobre paimbnails", "que hace este mod",
            "paimbnails"
        };
        it.searchPhrasesByLang["english"] = {
            "tell me about paimon", "what is paimon"
        };
        it.searchPhrasesByLang["spanish"] = {
            "hablame de paimon", "que es paimon"
        };
        it.responseByLang["english"] = fmt::format(
            "<cy>Paimbnails</c> is a Geometry Dash mod: thumbnails, capture, visual "
            "effects, audio, emotes, profiles and custom backgrounds. I know "
            "<cg>{}</c> features and you're on <cy>v{}</c>. Say a feature's name and "
            "I'll take you there!", featureCount, version);
        it.responseByLang["spanish"] = fmt::format(
            "<cy>Paimbnails</c> es un mod de Geometry Dash: miniaturas, captura, "
            "efectos visuales, audio, emotes, perfiles y fondos personalizados. "
            "Conozco <cg>{}</c> funciones y estas en <cy>v{}</c>. Dime el nombre de "
            "una y te llevo!", featureCount, version);
        m_intents.push_back(std::move(it));
    }
    {
        GuideIntent it;
        it.id = "mod-author";
        it.kind = IntentKind::Conversational;
        it.priority = 28;
        it.weight = 40;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "who made paimbnails", "who made you", "who created this",
            "who is the developer", "creator", "author", "developer"
        };
        it.keywordsByLang["spanish"] = {
            "quien hizo paimbnails", "quien te creo", "quien creo esto",
            "quien es el desarrollador", "creador", "autor", "desarrollador"
        };
        it.responseByLang["english"] =
            "Paimbnails is made by <cy>FlozWer</c>. The source is on GitHub and the "
            "community lives on Discord (links are in the Paimon Hub).";
        it.responseByLang["spanish"] =
            "Paimbnails lo hizo <cy>FlozWer</c>. El codigo esta en GitHub y la "
            "comunidad en Discord (los enlaces estan en el Paimon Hub).";
        m_intents.push_back(std::move(it));
    }
    {
        GuideIntent it;
        it.id = "mod-free";
        it.kind = IntentKind::Conversational;
        it.priority = 28;
        it.weight = 40;
        it.animation = GuideAnimation::Surprise;
        it.keywordsByLang["english"] = {
            "is it free", "is paimbnails free", "does it cost",
            "how much does it cost", "price", "is this free"
        };
        it.keywordsByLang["spanish"] = {
            "es gratis", "es gratuito", "cuesta dinero",
            "cuanto cuesta", "precio", "es de pago"
        };
        it.responseByLang["english"] =
            "Yep, <cg>Paimbnails is free</c> and open-source. Enjoy!";
        it.responseByLang["spanish"] =
            "Si, <cg>Paimbnails es gratis</c> y de codigo abierto. Disfrutalo!";
        m_intents.push_back(std::move(it));
    }
    {
        GuideIntent it;
        it.id = "mod-install";
        it.kind = IntentKind::Conversational;
        it.priority = 28;
        it.weight = 40;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "how to install", "how do i install", "install paimbnails",
            "installation", "how to get it"
        };
        it.keywordsByLang["spanish"] = {
            "como instalar", "como se instala", "instalar paimbnails",
            "instalacion", "como conseguirlo"
        };
        it.responseByLang["english"] =
            "Paimbnails installs through the <cy>Geode</c> mod loader (find it in the "
            "mod index). If we're chatting, it's already installed!";
        it.responseByLang["spanish"] =
            "Paimbnails se instala con el cargador de mods <cy>Geode</c> (esta en el "
            "indice de mods). Si estamos hablando, ya lo tienes instalado!";
        m_intents.push_back(std::move(it));
    }
    {
        GuideIntent it;
        it.id = "feature-list";
        it.kind = IntentKind::Conversational;
        it.priority = 32;
        it.weight = 42;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "list of features", "all features", "feature list", "list features",
            "what features does it have", "show me all features"
        };
        it.keywordsByLang["spanish"] = {
            "lista de funciones", "todas las funciones", "lista de features",
            "que funciones tiene", "muestrame todas las funciones", "todas las features"
        };
        it.responseByLang["english"] = fmt::format(
            "I cover profiles, backgrounds, menu music, cursor, Discord, thumbnails, "
            "pet, effects, quick hub, capture, texture studio and more - "
            "<cg>{} features</c> in total. Say a name to open it.", featureCount);
        it.responseByLang["spanish"] = fmt::format(
            "Cubro perfiles, fondos, musica del menu, cursor, Discord, miniaturas, "
            "mascota, efectos, quick hub, captura, texture studio y mas - "
            "<cg>{} funciones</c> en total. Dime un nombre para abrirla.", featureCount);
        m_intents.push_back(std::move(it));
    }
    {
        GuideIntent it;
        it.id = "mod-support";
        it.kind = IntentKind::Conversational;
        it.priority = 28;
        it.weight = 40;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "support", "contact", "report a bug", "report bug",
            "get help", "i found a bug", "need support"
        };
        it.keywordsByLang["spanish"] = {
            "soporte", "contacto", "reportar un error", "reportar bug",
            "encontre un error", "necesito soporte"
        };
        it.responseByLang["english"] =
            "For support open <cy>Paimon Hub > General > Soporte</c>, or join the "
            "Discord. Found a bug? Report it there!";
        it.responseByLang["spanish"] =
            "Para soporte abre <cy>Paimon Hub > General > Soporte</c>, o entra al "
            "Discord. Encontraste un bug? Reportalo ahi!";
        m_intents.push_back(std::move(it));
    }


    {
        GuideIntent it;
        it.id = "weather";
        it.kind = IntentKind::Conversational;
        it.priority = 12;
        it.weight = 22;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "what is the weather", "is it cold", "is it hot",
            "is it raining", "temperature"
        };
        it.keywordsByLang["spanish"] = {
            "que tiempo hace", "hace frio", "hace calor",
            "esta lloviendo", "temperatura"
        };
        it.searchPhrasesByLang["english"] = {
            "the weather", "weather today", "weather outside"
        };
        it.searchPhrasesByLang["spanish"] = {
            "el clima", "clima hoy", "tiempo afuera"
        };
        it.responseByLang["english"] =
            "I'd love to check, but I live inside your game: <cg>always sunny, "
            "always 25 degrees!</c>";
        it.responseByLang["spanish"] =
            "Me encantaria mirarlo, pero vivo dentro de tu juego: <cg>siempre soleado, "
            "siempre 25 grados!</c>";
        it.variantsByLang["english"] = {
            "No internet forecast from here - but the menu music is always playing!",
            "Check your window! In here it's eternal summer.",
        };
        it.variantsByLang["spanish"] = {
            "Aqui no hay pronostico - pero la musica del menu nunca para!",
            "Mira por tu ventana! Aqui dentro es verano eterno.",
        };
        m_intents.push_back(std::move(it));
    }

    {
        GuideIntent it;
        it.id = "favorite-things";
        it.kind = IntentKind::Conversational;
        it.priority = 12;
        it.weight = 22;
        it.animation = GuideAnimation::Surprise;
        it.keywordsByLang["english"] = {
            "what do you like", "whats your favorite", "what is your favorite",
            "favorite food", "favorite color", "do you like"
        };
        it.keywordsByLang["spanish"] = {
            "que te gusta", "cual es tu favorito", "cual es tu favorita",
            "comida favorita", "color favorito", "te gusta"
        };
        it.responseByLang["english"] =
            "My favorites: <cy>menu music</c>, <cy>shiny icons</c>, and anything that "
            "opens a popup! (Food? I'd say... <cg>apples</c>.)";
        it.responseByLang["spanish"] =
            "Mis favoritos: <cy>musica del menu</c>, <cy>iconos brillantes</c> y todo lo "
            "que abra un popup! (Comida? Diria... <cg>manzanas</c>.)";
        it.variantsByLang["english"] = {
            "I'm partial to clean thumbnails and smooth scrolls. What's yours?",
            "Anything with a nice gradient. And apples, of course.",
        };
        it.variantsByLang["spanish"] = {
            "Me encantan las miniaturas limpias y el scroll suave. Y a ti?",
            "Cualquier cosa con un buen degradado. Y manzanas, claro.",
        };
        m_intents.push_back(std::move(it));
    }

    {
        GuideIntent it;
        it.id = "feelings";
        it.kind = IntentKind::Conversational;
        it.priority = 12;
        it.weight = 22;
        it.animation = GuideAnimation::Surprise;
        it.keywordsByLang["english"] = {
            "are you sad", "are you happy", "are you tired", "do you sleep",
            "are you bored", "how do you feel"
        };
        it.keywordsByLang["spanish"] = {
            "estas triste", "estas feliz", "estas cansada", "duermes",
            "te aburres", "como te sientes"
        };
        it.responseByLang["english"] =
            "I don't sleep - I just float! And I'm always happy when you ask me "
            "something. <cg>Let's keep going!</c>";
        it.responseByLang["spanish"] =
            "No duermo, solo floto! Y estoy siempre feliz cuando me preguntas algo. "
            "<cg>Sigamos!</c>";
        it.variantsByLang["english"] = {
            "Sleep? I've been awake since v1.0.1!",
            "Happy as a cube with a checkered pattern.",
        };
        it.variantsByLang["spanish"] = {
            "Dormir? Despierta desde la v1.0.1!",
            "Feliz como un cubo con patron de ajedrez.",
        };
        m_intents.push_back(std::move(it));
    }

    {
        GuideIntent it;
        it.id = "capabilities";
        it.kind = IntentKind::Conversational;
        it.priority = 14;
        it.weight = 24;
        it.animation = GuideAnimation::Talk;
        it.keywordsByLang["english"] = {
            "can you play", "can you do", "are you smart", "are you an ai",
            "are you a bot", "do you know everything"
        };
        it.keywordsByLang["spanish"] = {
            "puedes jugar", "puedes hacer", "eres inteligente", "eres una ia",
            "eres un bot", "sabes de todo"
        };
        it.responseByLang["english"] =
            "I'm a <cy>local brain</c>: no internet, no servers. I know Paimbnails "
            "inside out and I'll guide you through it.";
        it.responseByLang["spanish"] =
            "Soy un <cy>cerebro local</c>: sin internet, sin servidores. Conozco "
            "Paimbnails de arriba a abajo y te guio por todo.";
        it.variantsByLang["english"] = {
            "Smart enough to open any popup! That counts, right?",
            "I can't play levels, but I can take you to anything in the mod.",
        };
        it.variantsByLang["spanish"] = {
            "Lo bastante lista para abrir cualquier popup! Cuenta, no?",
            "No puedo jugar niveles, pero te llevo a cualquier cosa del mod.",
        };
        m_intents.push_back(std::move(it));
    }

    {
        GuideIntent it;
        it.id = "origin";
        it.kind = IntentKind::Conversational;
        it.priority = 12;
        it.weight = 22;
        it.animation = GuideAnimation::Wave;
        it.keywordsByLang["english"] = {
            "where are you from", "where do you live", "where were you born",
            "are you from teyvat", "your home"
        };
        it.keywordsByLang["spanish"] = {
            "de donde eres", "donde vives", "donde naciste",
            "eres de teyvat", "tu hogar"
        };
        it.responseByLang["english"] =
            "I'm from <cy>Teyvat</c> originally... but these days I live inside "
            "Paimbnails, floating over Geometry Dash!";
        it.responseByLang["spanish"] =
            "Soy de <cy>Teyvat</c> originalmente... pero hoy vivo dentro de Paimbnails, "
            "flotando sobre Geometry Dash!";
        it.variantsByLang["english"] = {
            "Home is wherever the pixels are.",
            "I migrated from Teyvat to your menu screen.",
        };
        it.variantsByLang["spanish"] = {
            "Mi hogar es donde esten los pixeles.",
            "Migre de Teyvat a tu pantalla de menu.",
        };
        m_intents.push_back(std::move(it));
    }
}

GuideRecommendation PaimonGuideService::makeRecommendation(
    std::string const& intentId,
    std::string const& langId) const {
    GuideRecommendation rec;
    rec.intentId = intentId;
    rec.label = PopupRegistry::get().displayNameFor(intentId, langId);
    if (auto const* entry = PopupRegistry::get().findById(intentId)) {
        rec.action = entry->open;
    } else {
        for (auto const& it : m_intents) {
            if (it.id == intentId) {
                rec.action = it.action;
                break;
            }
        }
    }
    return rec;
}

void PaimonGuideService::attachRelatedRecommendations(
    GuideAnswer& ans,
    GuideIntent const& primary,
    GuideIntent const* runnerUp,
    std::string const& langId,
    int maxExtra) const {
    if (maxExtra <= 0) return;

    auto already = [&](std::string const& id) {
        if (id == primary.id) return true;
        for (auto const& r : ans.recommendations) {
            if (r.intentId == id) return true;
        }
        return false;
    };

    if (runnerUp && runnerUp->kind == IntentKind::Functional
        && !already(runnerUp->id)) {
        ans.recommendations.push_back(makeRecommendation(runnerUp->id, langId));
        if (static_cast<int>(ans.recommendations.size()) >= maxExtra) return;
    }

    if (!primary.categoryId.empty()) {
        auto cat = categoryFromId(primary.categoryId);
        if (cat != PopupCategory::None) {
            for (auto const* e : PopupRegistry::get().entriesInCategory(cat)) {
                if (!e || already(e->id)) continue;
                ans.recommendations.push_back(makeRecommendation(e->id, langId));
                if (static_cast<int>(ans.recommendations.size()) >= maxExtra) return;
            }
        }
    }
}

std::optional<GuideAnswer> PaimonGuideService::tryCategoryBrowse(
    std::string const& normalized,
    std::vector<std::string> const& tokens,
    std::string const& langId) const {
    struct CatHint {
        PopupCategory cat;
        char const* en;
        char const* es;
    };
    static CatHint const kHints[] = {
        { PopupCategory::Music,      "music",     "musica" },
        { PopupCategory::Profile,    "profile",   "perfil" },
        { PopupCategory::Background, "background","fondo" },
        { PopupCategory::Background, "backgrounds","fondos" },
        { PopupCategory::Thumbnail,  "thumbnail", "miniatura" },
        { PopupCategory::Thumbnail,  "thumbnails","miniaturas" },
        { PopupCategory::Cursor,     "cursor",    "cursor" },
        { PopupCategory::Discord,    "discord",   "discord" },
        { PopupCategory::Pet,        "pet",       "mascota" },
        { PopupCategory::Emote,      "emote",     "emote" },
        { PopupCategory::Emote,      "emotes",    "emotes" },
        { PopupCategory::Capture,    "capture",   "captura" },
        { PopupCategory::Transition, "transition","transicion" },
        { PopupCategory::Transition, "transitions","transiciones" },
        { PopupCategory::Layout,     "layout",    "layout" },
        { PopupCategory::Volume,     "volume",    "volumen" },
        { PopupCategory::QuickHub,   "quickhub",  "quickhub" },
        { PopupCategory::Forum,      "forum",     "foro" },
        { PopupCategory::Forum,      "community", "comunidad" },
        { PopupCategory::Editor,     "editor",    "editor" },
        { PopupCategory::Visuals,    "visuals",   "visuales" },
        { PopupCategory::Visuals,    "effects",   "efectos" },
        { PopupCategory::Visuals,    "shaders",   "shaders" },
        { PopupCategory::Update,     "update",    "actualizar" },
        { PopupCategory::Cache,      "settings",  "ajustes" },
    };

    auto content = LightLemmatizer::removeStopwords(tokens);
    if (content.empty() || content.size() > 3) return std::nullopt;

    bool browsePhrase = false;
    if (normalized.find("stuff") != std::string::npos
        || normalized.find("things") != std::string::npos
        || normalized.find("cosas") != std::string::npos
        || normalized.find("todo de") != std::string::npos
        || normalized.find("todas las") != std::string::npos
        || normalized.find("all about") != std::string::npos
        || normalized.find("options for") != std::string::npos) {
        browsePhrase = true;
    }

    PopupCategory found = PopupCategory::None;
    for (auto const& t : content) {
        auto forms = LightLemmatizer::expand(t);
        for (auto const& f : forms) {
            for (auto const& h : kHints) {
                if (f == h.en || f == h.es || t == h.en || t == h.es) {
                    found = h.cat;
                    break;
                }
            }
            if (found != PopupCategory::None) break;
        }
        if (found != PopupCategory::None) break;
    }
    if (found == PopupCategory::None) return std::nullopt;

    // Browse requires explicit phrasing; bare terms use precise alias matching.
    if (!browsePhrase) return std::nullopt;

    auto members = PopupRegistry::get().entriesInCategory(found);
    if (members.size() < 2) return std::nullopt;

    auto const* lead = members.front();
    bool es = (langId == "spanish");
    auto catName = categoryDisplayName(found, langId);

    std::string list;
    int listed = 0;
    for (auto const* e : members) {
        if (!e) continue;
        if (listed > 0) list += es ? ", " : ", ";
        list += "<cy>" + PopupRegistry::get().displayNameFor(e->id, langId) + "</c>";
        if (++listed >= 5) break;
    }

    GuideAnswer ans;
    ans.found = true;
    ans.animation = GuideAnimation::Point;
    ans.matchedIntentId = lead->id;
    ans.action = lead->open;
    ans.message = es
        ? ("En <cy>" + catName + "</c> tengo: " + list
           + ". Te llevo a <cy>"
           + PopupRegistry::get().displayNameFor(lead->id, langId)
           + "</c>; toca un chip para otra.")
        : ("Under <cy>" + catName + "</c> I have: " + list
           + ". I'll open <cy>"
           + PopupRegistry::get().displayNameFor(lead->id, langId)
           + "</c>; tap a chip for another.");

    for (std::size_t i = 1; i < members.size() && ans.recommendations.size() < 3; ++i) {
        ans.recommendations.push_back(
            makeRecommendation(members[i]->id, langId));
    }
    return ans;
}

GuideAnswer PaimonGuideService::makeFallback(
    std::vector<GuideIntent const*> const& suggestions,
    std::string const& langId) const {
    GuideAnswer ans;
    ans.found = false;
    ans.animation = GuideAnimation::Sleep;

    if (!suggestions.empty()) {
        auto& reg = PopupRegistry::get();
        std::vector<std::string> names;
        for (auto const* it : suggestions) {
            if (!it) continue;
            auto n = reg.displayNameFor(it->id, langId);
            if (!n.empty()) names.push_back("<cy>" + n + "</c>");
            if (ans.recommendations.size() < 3) {
                ans.recommendations.push_back(makeRecommendation(it->id, langId));
            }
        }
        if (!names.empty()) {
            bool es = (langId == "spanish");
            std::string joined = names[0];
            if (names.size() >= 2) {
                joined += es ? " o " : " or ";
                joined += names[1];
            }
            ans.animation = GuideAnimation::Talk;
            if (!ans.recommendations.empty() && ans.recommendations.front().action) {
                ans.action = ans.recommendations.front().action;
                ans.matchedIntentId = ans.recommendations.front().intentId;
                if (ans.recommendations.size() > 1) {
                    ans.recommendations.erase(ans.recommendations.begin());
                } else {
                    ans.recommendations.clear();
                }
            }
            ans.message = es
                ? "Mmm, no estoy segura. Quizas querias " + joined + "?"
                : "Hmm, not sure. Did you mean " + joined + "?";
            return ans;
        }
    }

    ans.message = tr(
        "pai.guide.fallback",
        "Hmm, I don't know that one. Try keywords like cursor, music, "
        "background, discord, emotes, profile..."
    );
    return ans;
}

namespace {

// Rotate variants for repeated intents within 60 seconds.
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
            std::size_t idx = static_cast<std::size_t>(repeatCount - 1)
                              % vIt->second.size();
            return vIt->second[idx];
        }
        auto rIt = intent.responseByLang.find(lang);
        if (rIt != intent.responseByLang.end()) return rIt->second;
        return {};
    };

    auto str = pickFromLang(langId);
    if (str.empty()) str = pickFromLang("english");
    return str;
}

// Combine topics, opening the first and suggesting the rest.
GuideAnswer makeMultiTopicAnswer(std::vector<GuideIntent const*> const& topics,
                                 std::string const& langId) {
    auto& reg = PopupRegistry::get();
    bool es = (langId == "spanish");

    std::vector<std::string> names;
    for (auto const* t : topics) names.push_back(reg.displayNameFor(t->id, langId));

    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) list += (i + 1 == names.size()) ? (es ? " y " : " and ") : ", ";
        list += "<cy>" + names[i] + "</c>";
    }

    GuideAnswer ans;
    ans.found = true;
    ans.animation = GuideAnimation::Point;
    ans.matchedIntentId = topics.front()->id;
    ans.action = topics.front()->action;
    ans.message = es
        ? ("Mencionaste " + list + ". Te llevo primero a <cy>" + names.front()
           + "</c>; toca un chip para el resto!")
        : ("You mentioned " + list + ". I'll open <cy>" + names.front()
           + "</c> first; tap a chip for the rest!");

    for (std::size_t i = 1; i < topics.size() && ans.recommendations.size() < 3; ++i) {
        GuideRecommendation rec;
        rec.intentId = topics[i]->id;
        rec.label = names[i];
        rec.action = topics[i]->action;
        ans.recommendations.push_back(std::move(rec));
    }
    return ans;
}

}

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

    if (intent.kind == IntentKind::Functional) {
        if (auto const* top = m_engine.topic(intent.id)) {
            bool es = (langId == "spanish");
            for (auto const& s : top->subtopics) {
                GuideRecommendation rec;
                rec.intentId = intent.id + "." + s.id;
                rec.label = es ? s.esHint : s.enHint;
                std::string query = es ? "y " + s.esHint + "?" : "and " + s.enHint + "?";
                rec.action = [query](PaimonGuideChatPopup* popup) {
                    if (popup) popup->submitQuery(query);
                };
                ans.recommendations.push_back(std::move(rec));
                if (ans.recommendations.size() >= 3) break;
            }
        }
    }

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

GuideAnswer PaimonGuideService::buildContextualAnswer(
    Resolution const& res,
    std::string const& langId)
{
    bool es = (langId == "spanish");
    GuideAnswer ans;
    ans.found = true;
    ans.animation = GuideAnimation::Talk;
    ans.matchedIntentId = res.topicId;

    auto const* top = m_engine.topic(res.topicId);
    auto const* sub = res.subTopicId.empty()
        ? nullptr
        : m_engine.subTopic(res.topicId, res.subTopicId);

    std::string topicName = top
        ? (es ? top->esName : top->enName)
        : PopupRegistry::get().displayNameFor(res.topicId, langId);

    // "que mas?" / "what else?" → the topic's more reply.
    bool isMore = res.pureReference && !res.subTopicId.empty();
    (void)isMore;

    if (res.subTopicId.empty()) {
        // Pure reference: "y eso?" — answer with the topic's more reply.
        ans.message = (es ? "Siguiendo con <cy>" : "Following up on <cy>")
            + topicName + "</c>: ";
        if (top && !top->esMoreReply.empty() && es) {
            ans.message += top->esMoreReply;
        } else if (top && !top->enMoreReply.empty()) {
            ans.message += top->enMoreReply;
        } else {
            ans.message += (es
                ? "Puedes preguntarme por sus opciones (por ejemplo <cy>color</c>, <cy>tamano</c>...)."
                : "You can ask me about its options (e.g. <cy>color</c>, <cy>size</c>...).");
        }
    } else if (sub) {
        ans.message = (es ? "Siguiendo con <cy>" : "Following up on <cy>")
            + topicName + "</c>: ";
        ans.message += es ? sub->esReply : sub->enReply;
    } else {
        // Sub-topic not found in knowledge: fall back to the topic itself.
        ans.message = (es ? "Sobre <cy>" : "About <cy>") + topicName + "</c>: "
            + (top ? (es ? top->esMoreReply : top->enMoreReply)
                   : (es ? "Preguntame sus opciones." : "Ask me about its options."));
    }

    if (top) {
        for (auto const& s : top->subtopics) {
            if (s.id == res.subTopicId) continue;
            GuideRecommendation rec;
            rec.intentId = res.topicId + "." + s.id;
            rec.label = es ? s.esHint : s.enHint;
            std::string query = es ? "y " + s.esHint + "?" : "and " + s.enHint + "?";
            rec.action = [query](PaimonGuideChatPopup* popup) {
                if (popup) popup->submitQuery(query);
            };
            ans.recommendations.push_back(std::move(rec));
            if (ans.recommendations.size() >= 3) break;
        }
    }

    return ans;
}

GuideAnswer PaimonGuideService::ask(std::string const& userQuery, AskCallback callback) {
    auto langId = Localization::get().getCurrentLanguageId();
    auto normalized = normalize(userQuery);

    if (normalized.empty()) {
        return makeFallback({}, langId);
    }

     // Max mode sends the thread to Gemini asynchronously.
    if (getMode() == GuideMode::Max) {
        std::vector<GeminiClient::ChatMessage> history;
        auto const& turns = m_memory.history();
        for (auto const& t : turns) {
            if (t.userQuery.empty()) continue;
            history.push_back({"user", t.userQuery});
        }
        history.push_back({"user", userQuery});

        auto& reg = PopupRegistry::get();
        std::string featureList;
        {
            int listed = 0;
            for (auto const& entry : reg.entries()) {
                if (!entry.open) continue;
                if (listed > 0) featureList += ", ";
                featureList += entry.id;
                if (++listed >= 60) break;
            }
        }

        std::string systemPrompt =
            "You are Paimon, the tiny floating guide inside the Geometry Dash mod "
            "Paimbnails. You help players find features of the mod (cursor, menu "
            "music, thumbnails, capture, profiles, editor tools, collab, etc). "
            "Answer in the same language the user writes in (Spanish or English). "
            "Be short, friendly and in character. Use emoji sparingly.\n"
            "You can OPEN the configuration of a feature by yourself. The user can "
            "ask things like \"abre la configuracion del cursor\", \"open menu "
            "music\", \"llevame a los ajustes de miniaturas\". When they do, or when "
            "opening the feature's settings would clearly help, append at the very "
            "END of your answer the marker [[abrir:FEATURE_ID]] (only one), where "
            "FEATURE_ID is one of exactly these ids (English, lowercase): " +
            featureList + ".\n"
            "Only use a marker if the user explicitly wants to open/configure "
            "something or it clearly helps. If unsure, don't use it. Never invent "
            "an id that is not in the list. The marker is invisible to the user.";

        GuideAnswer thinking;
        thinking.found = true;
        thinking.matchedIntentId = "";
        thinking.animation = GuideAnimation::Talk;
        bool es = (langId == "spanish");
        thinking.message = es
            ? "Hmm, dejame pensarlo con mi modo <cy>Max</c>..."
            : "Hmm, let me think with my <cy>Max</c> mode...";

        GeminiClient::get().complete(history, systemPrompt,
            [this, callback, langId](bool success, std::string const& reply) {
                GuideAnswer ans;
                ans.found = success;
                ans.animation = GuideAnimation::Talk;
                if (success) {
                    ans.message = reply;

                    std::string text = reply;
                    auto openPos = text.find("[[abrir:");
                    if (openPos != std::string::npos) {
                        auto idStart = openPos + 8;
                        auto idEnd = text.find("]]", idStart);
                        if (idEnd != std::string::npos) {
                            std::string featureId = text.substr(idStart, idEnd - idStart);
                            while (!featureId.empty()
                                   && (featureId.front() == ' '
                                       || featureId.front() == '\t'
                                       || featureId.front() == '\n')) {
                                featureId.erase(featureId.begin());
                            }
                            while (!featureId.empty()
                                   && (featureId.back() == ' '
                                       || featureId.back() == '\t'
                                       || featureId.back() == '\n')) {
                                featureId.pop_back();
                            }
                            ans.message = text.substr(0, openPos)
                                + text.substr(idEnd + 2);
                            for (auto const& it : m_intents) {
                                if (it.id == featureId && it.action) {
                                    ans.action = it.action;
                                    ans.matchedIntentId = featureId;
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    bool es = (langId == "spanish");
                    if (reply == "unavailable") {
                        ans.message = es
                            ? "El modo <cy>Max</c> no esta disponible por ahora. "
                              "Sigo respondiendo con mi modo <cy>Asistente</c>."
                            : "<cy>Max</c> mode is not available right now. "
                              "I'll keep answering with my <cy>Assistant</c> mode.";
                    } else {
                        ans.message = es
                            ? "Mmm, no pude conectar con mi modo Max. "
                              "Detalle: " + reply
                            : "Hmm, I couldn't reach my Max mode. "
                              "Detail: " + reply;
                    }
                }
                if (callback) callback(ans);
            });
        return thinking;
    }

    if (ConversationMemory::looksLikeFollowUp(normalized)) {
        if (auto last = m_memory.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < ConversationMemory::kRecentSecs)
        {
            for (auto const& intent : m_intents) {
                if (intent.id == last->matchedIntentId) {
                    auto ans = buildFollowUpAnswer(intent, langId);
                    ConversationTurn turn;
                    turn.userQuery = userQuery;
                    turn.normalizedQuery = normalized;
                    turn.matchedIntentId = intent.id;
                    turn.wasFunctional = (intent.kind == IntentKind::Functional);
                    turn.matchScore = 100.0;
                    m_memory.recordTurn(std::move(turn));
                    return ans;
                }
            }
        }
    }

    auto tokens = tokenize(normalized);

    {
        auto content = LightLemmatizer::removeStopwords(tokens);
        std::string ctx = currentTopicId();
        if (!ctx.empty()) {
            auto res = m_engine.resolve(normalized, content, langId, ctx);
            if (res.isFollowUp) {
                auto ans = buildContextualAnswer(res, langId);
                ConversationTurn turn;
                turn.userQuery = userQuery;
                turn.normalizedQuery = normalized;
                turn.matchedIntentId = res.topicId;
                turn.topicId = res.topicId;
                turn.wasFunctional = true;
                turn.matchScore = 100.0;
                m_memory.recordTurn(std::move(turn));
                return ans;
            }
        }
    }

    if (auto topics = PaigoritV1::splitTopics(m_intents, normalized, langId);
        topics.size() >= 2) {
        auto ans = makeMultiTopicAnswer(topics, langId);
        ConversationTurn turn;
        turn.userQuery = userQuery;
        turn.normalizedQuery = normalized;
        turn.matchedIntentId = topics.front()->id;
        turn.wasFunctional = true;
        turn.matchScore = 100.0;
        m_memory.recordTurn(std::move(turn));
        return ans;
    }

    if (auto browse = tryCategoryBrowse(normalized, tokens, langId)) {
        ConversationTurn turn;
        turn.userQuery = userQuery;
        turn.normalizedQuery = normalized;
        turn.matchedIntentId = browse->matchedIntentId;
        turn.wasFunctional = true;
        turn.matchScore = 100.0;
        m_memory.recordTurn(std::move(turn));
        return *browse;
    }


    auto paigorit = PaigoritV1::run(m_intents, normalized, tokens, langId);

    GuideIntent const* best = paigorit.best;
    double bestRaw = paigorit.bestRawFuzzy;

    if (!paigorit.ranking.empty()) {
        log::debug("Paigorit V1 query='{}' top results:", normalized);
        std::size_t logged = 0;
        for (auto const& s : paigorit.ranking) {
            if (logged >= 3) break;
            log::debug("  [{}] id={} tier={} weight={} fuzzy={:.1f} anchored={:.1f} "
                       "compound={} exact={} full={} coverage={:.2f} final={:.2f}",
                       logged, s.intent->id, s.tier, s.intent->weight,
                       s.bestKeywordFuzzy, s.bestAnchoredFuzzy,
                       s.hasCompoundMatch ? "yes" : "no",
                       s.hasExactTokenMatch ? "yes" : "no",
                       s.hasFullExactMatch ? "yes" : "no",
                       s.coverageRatio, s.finalScore);
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

        GuideIntent const* runner = nullptr;
        if (paigorit.ambiguous && paigorit.runnerUp
            && paigorit.runnerUp->id != best->id
            && best->kind == IntentKind::Functional
            && paigorit.runnerUp->kind == IntentKind::Functional) {
            runner = paigorit.runnerUp;
            auto alt = PopupRegistry::get().displayNameFor(
                paigorit.runnerUp->id, langId);
            if (!alt.empty()) {
                ans.message += (langId == "spanish")
                    ? "\n(O quizas querias <cy>" + alt + "</c>?)"
                    : "\n(Or did you mean <cy>" + alt + "</c>?)";
            }
        }

        if (best->kind == IntentKind::Functional) {
            attachRelatedRecommendations(ans, *best, runner, langId, 3);
        }
    } else {
        bool es = (langId == "spanish");
        std::string ctx = currentTopicId();
        if (paigorit.suggestions.empty() && !ctx.empty()) {
            auto const* top = m_engine.topic(ctx);
            if (top) {
                Resolution res;
                res.isFollowUp = true;
                res.topicId = ctx;
                res.pureReference = true;
                ans = buildContextualAnswer(res, langId);
                ans.found = false;
                ans.message = (es
                    ? "Mmm, no entendi del todo. "
                    : "Hmm, I didn't quite get that. ") + ans.message;
            } else {
                ans = makeFallback(paigorit.suggestions, langId);
            }
        } else {
            ans = makeFallback(paigorit.suggestions, langId);
        }
    }

    ConversationTurn turn;
    turn.userQuery = userQuery;
    turn.normalizedQuery = normalized;
    turn.matchedIntentId = best ? best->id : "";
    turn.wasFunctional = best && (best->kind == IntentKind::Functional);
    turn.matchScore = bestRaw;
    m_memory.recordTurn(std::move(turn));

    return ans;
}

std::vector<std::pair<std::string, std::string>>
PaimonGuideService::getSuggestions() {
    auto langId = Localization::get().getCurrentLanguageId();
    bool es = (langId == "spanish");

    std::vector<std::pair<std::string, std::string>> result;
    if (es) {
        result.push_back({"cursor",   "donde configuro el cursor"});
        result.push_back({"musica",   "como pongo musica de menu"});
        result.push_back({"fondos",   "donde cambio los fondos"});
        result.push_back({"iconos",   "recolorear mis iconos"});
        result.push_back({"captura",  "capturar mi nivel"});
        result.push_back({"collab",   "editor collab"});
    } else {
        result.push_back({"cursor",   "where do i configure cursor"});
        result.push_back({"music",    "how do i set menu music"});
        result.push_back({"bg",       "where do i change backgrounds"});
        result.push_back({"icons",    "recolor my icons"});
        result.push_back({"capture",  "capture my level"});
        result.push_back({"collab",   "collab editor"});
    }
    return result;
}

}
