#include "LightLemmatizer.hpp"

#include <algorithm>

namespace paimon::guide {

// ─────────────────────────────────────────────────────────────────────────────
// Stopwords compartidas EN/ES (normalizadas: lowercase, sin acentos)
// ─────────────────────────────────────────────────────────────────────────────

std::unordered_set<std::string> const& LightLemmatizer::stopwords() {
    static const std::unordered_set<std::string> kStopwords = {
        // Articulos / pronombres / preposiciones EN
        "the", "a", "an", "is", "are", "be", "to", "of", "in", "on",
        "at", "by", "for", "with", "and", "or", "but", "i", "you",
        "we", "it", "this", "that", "those", "these", "do", "does",
        "did", "can", "should", "would", "could", "will", "shall",
        // Particulas interrogativas EN
        "what", "where", "when", "how", "who", "why", "which",
        // Articulos / pronombres / preposiciones ES
        "el", "la", "los", "las", "un", "una", "unos", "unas",
        "de", "del", "al", "a", "en", "con", "por", "para",
        "sin", "sobre", "y", "o", "u", "e", "ni", "pero", "yo",
        "tu", "el", "ella", "nosotros", "vosotros", "ellos", "ellas",
        "es", "esta", "estan", "ser", "estar", "esto", "eso", "aquello",
        "ese", "esa", "este", "esa", "aquel",
        // Particulas interrogativas ES
        "donde", "que", "quien", "cuando", "como", "porque", "cual",
        "cuanto", "cuantos", "cuanta", "cuantas",
        // Verbos auxiliares comunes que no aportan info
        "hay", "tiene", "tengo", "tienes", "puedo", "puede", "puedes",
        "sabes", "quiero", "quieres", "necesito", "ayuda",
        // Filler EN
        "please", "me", "my", "your", "any",
        // Filler ES
        "porfavor", "por favor", "mi", "tu", "su", "alguno", "algun",
    };
    return kStopwords;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sinonimos / aliases comunes
// ─────────────────────────────────────────────────────────────────────────────

std::unordered_map<std::string, std::string> const& LightLemmatizer::synonyms() {
    static const std::unordered_map<std::string, std::string> kSyn = {
        // EN abreviaturas
        {"pic",     "picture"},
        {"pfp",     "profile"},
        {"avatar",  "profile"},
        {"bg",      "background"},
        {"rpc",     "discord"},
        {"sfx",     "audio"},
        {"qh",      "quickhub"},
        {"thumb",   "thumbnail"},
        {"thumbs",  "thumbnail"},
        // EN variantes
        {"song",       "music"},
        {"songs",      "music"},
        {"musics",     "music"},
        {"backgrounds","background"},
        {"profiles",   "profile"},
        {"emoji",      "emote"},
        {"emojis",     "emote"},
        {"emoticon",   "emote"},
        // ES abreviaturas y variantes
        {"perfilar",   "perfil"},
        {"perfiles",   "perfil"},
        {"fondos",     "fondo"},
        {"fondo",      "background"}, // colapsamos a la canonica EN para
                                      // unificar matching cross-lingual
        {"musica",     "music"},
        {"musicas",    "music"},
        {"cancion",    "music"},
        {"canciones",  "music"},
        {"miniatura",  "thumbnail"},
        {"miniaturas", "thumbnail"},
        {"foto",       "picture"},
        {"fotos",      "picture"},
        {"imagen",     "picture"},
        {"imagenes",   "picture"},
        {"raton",      "cursor"},
        {"puntero",    "cursor"},
        {"ayuda",      "help"},   // marcado tambien como stopword pero no
                                  // pasara por aqui (ver isStopword)
        {"mascota",    "pet"},
        {"foro",       "forum"},
        {"comunidad",  "forum"},
        {"actualizar", "update"},
        {"actualizacion", "update"},
        {"version",    "update"},
        {"idioma",     "language"},
        {"lenguaje",   "language"},
        {"volumen",    "volume"},
        {"sonido",     "audio"},
        {"transicion", "transition"},
        {"transiciones","transition"},
        {"capturar",   "capture"},
        {"captura",    "capture"},
        {"vinilo",     "menumusic"},
    };
    return kSyn;
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

bool LightLemmatizer::isStopword(std::string const& tokenLower) {
    return stopwords().contains(tokenLower);
}

// Stemming muy ligero: recorta sufijos comunes EN y ES.
// Reglas, en orden:
//   - longitud >= 5 y termina en "ando" / "iendo" -> elimina sufijo (ES gerundio)
//   - longitud >= 5 y termina en "mente"          -> elimina sufijo (ES adverbio)
//   - longitud >= 5 y termina en "cion"           -> elimina (ES sustantivo abstracto)
//   - longitud >= 4 y termina en "ing"            -> elimina (EN gerundio)
//   - longitud >= 4 y termina en "ed"             -> elimina (EN pasado regular)
//   - longitud >= 4 y termina en "es"             -> elimina (plural EN/ES)
//   - longitud >= 4 y termina en "s"              -> elimina (plural simple)
// Aplicamos solo UNA regla (la primera que matchee), para evitar over-stem.
//
// No queremos un Porter completo; con esto cubrimos el ~80% de variantes
// que escriben los usuarios en queries cortas.
std::string LightLemmatizer::stem(std::string const& t) {
    if (t.size() < 4) return t;

    auto endsWith = [&](std::string const& suffix) {
        if (t.size() <= suffix.size()) return false;
        return std::equal(suffix.rbegin(), suffix.rend(), t.rbegin());
    };

    if (t.size() >= 6 && endsWith("iendo")) return t.substr(0, t.size() - 5);
    if (t.size() >= 5 && endsWith("ando"))  return t.substr(0, t.size() - 4);
    if (t.size() >= 6 && endsWith("mente")) return t.substr(0, t.size() - 5);
    if (t.size() >= 5 && endsWith("cion"))  return t.substr(0, t.size() - 4);
    if (t.size() >= 4 && endsWith("ing"))   return t.substr(0, t.size() - 3);
    if (t.size() >= 4 && endsWith("ed"))    return t.substr(0, t.size() - 2);
    if (t.size() >= 4 && endsWith("es"))    return t.substr(0, t.size() - 2);
    if (t.size() >= 4 && endsWith("s"))     return t.substr(0, t.size() - 1);

    return t;
}

std::vector<std::string> LightLemmatizer::expand(std::string const& token) {
    std::vector<std::string> out;
    if (token.empty()) return out;

    auto add = [&](std::string const& s) {
        if (s.empty()) return;
        if (std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    };

    if (isStopword(token)) return out;

    // Token original
    add(token);
    // Stem del original
    add(stem(token));

    // Sinonimo (si existe)
    auto const& syn = synonyms();
    auto it = syn.find(token);
    if (it != syn.end()) {
        add(it->second);
        add(stem(it->second));
    }

    // Tambien probar stem como clave de sinonimo (ej. "musicas" -> stem
    // "musica" -> sinonimo "music")
    auto stemmed = stem(token);
    if (stemmed != token) {
        auto it2 = syn.find(stemmed);
        if (it2 != syn.end()) {
            add(it2->second);
            add(stem(it2->second));
        }
    }

    return out;
}

std::vector<std::string> LightLemmatizer::removeStopwords(
    std::vector<std::string> const& tokens)
{
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (auto const& t : tokens) {
        if (!isStopword(t)) out.push_back(t);
    }
    return out;
}

std::vector<std::string> LightLemmatizer::tokenizeNoStopwords(
    std::string const& normalizedLower)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : normalizedLower) {
        if (c == ' ') {
            if (!cur.empty()) {
                if (!isStopword(cur)) tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty() && !isStopword(cur)) tokens.push_back(cur);
    return tokens;
}

} // namespace paimon::guide
