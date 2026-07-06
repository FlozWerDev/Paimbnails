#include "LightLemmatizer.hpp"

#include <algorithm>

namespace paimon::guide {

// Shared EN/ES stopwords (normalized: lowercase, no accents)

std::unordered_set<std::string> const& LightLemmatizer::stopwords() {
    static const std::unordered_set<std::string> kStopwords = {
        // Articles / pronouns / prepositions (EN)
        "the", "a", "an", "is", "are", "be", "to", "of", "in", "on",
        "at", "by", "for", "with", "and", "or", "but", "i", "you",
        "we", "it", "this", "that", "those", "these", "do", "does",
        "did", "can", "should", "would", "could", "will", "shall",
        // Question words (EN)
        "what", "where", "when", "how", "who", "why", "which",
        // Articles / pronouns / prepositions (ES)
        "el", "la", "los", "las", "un", "una", "unos", "unas",
        "de", "del", "al", "a", "en", "con", "por", "para",
        "sin", "sobre", "y", "o", "u", "e", "ni", "pero", "yo",
        "tu", "el", "ella", "nosotros", "vosotros", "ellos", "ellas",
        "es", "esta", "estan", "ser", "estar", "esto", "eso", "aquello",
        "ese", "esa", "este", "esa", "aquel",
        // Question words (ES)
        "donde", "que", "quien", "cuando", "como", "porque", "cual",
        "cuanto", "cuantos", "cuanta", "cuantas",
        // Common auxiliary verbs with no info
        "hay", "tiene", "tengo", "tienes", "puedo", "puede", "puedes",
        "sabes", "quiero", "quieres", "necesito", "ayuda",
        // Generic action verbs that carry no topic info (EN). Note: deliberately
        // NOT including registry alias words like "config", "capture", "update".
        "configure", "change", "set", "enable", "disable", "open", "find",
        "use", "want", "need", "show", "make", "give", "tell",
        // Generic action verbs (ES)
        "configurar", "cambiar", "poner", "activar", "desactivar", "abrir",
        "encontrar", "usar", "mostrar", "dame", "dime", "hacer",
        // Filler EN
        "please", "me", "my", "your", "any",
        // Filler ES
        "porfavor", "por favor", "mi", "tu", "su", "alguno", "algun",
    };
    return kStopwords;
}

// Common synonyms / aliases

std::unordered_map<std::string, std::string> const& LightLemmatizer::synonyms() {
    static const std::unordered_map<std::string, std::string> kSyn = {
        // EN abbreviations
        {"pic",     "picture"},
        {"pfp",     "profile"},
        {"avatar",  "profile"},
        {"bg",      "background"},
        {"rpc",     "discord"},
        {"sfx",     "audio"},
        {"qh",      "quickhub"},
        {"thumb",   "thumbnail"},
        {"thumbs",  "thumbnail"},
        // EN variants
        {"song",       "music"},
        {"songs",      "music"},
        {"musics",     "music"},
        {"soundtrack", "music"},
        {"track",      "music"},
        {"tune",       "music"},
        {"tunes",      "music"},
        {"wallpaper",  "background"},
        {"wallpapers", "background"},
        {"backgrounds","background"},
        {"profiles",   "profile"},
        {"emoji",      "emote"},
        {"emojis",     "emote"},
        {"emoticon",   "emote"},
        {"mouse",      "cursor"},
        {"pointer",    "cursor"},
        {"mascot",     "pet"},
        {"companion",  "pet"},
        // ES abbreviations and variants
        {"perfilar",   "perfil"},
        {"perfiles",   "perfil"},
        {"fondos",     "fondo"},
        {"fondo",      "background"}, // collapse to the EN canonical to unify cross-lingual matching
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
        {"ayuda",      "help"},   // also a stopword, so this won't be hit (see isStopword)
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
        {"papel",      "background"},  // "papel tapiz" / "papel de pantalla"
        {"tapiz",      "background"},
        {"companero",  "pet"},
        {"rueda",      "quickhub"},
        {"radial",     "quickhub"},
        {"playlists",  "playlist"},
        {"barra",      "progressbar"},
    };
    return kSyn;
}

bool LightLemmatizer::isStopword(std::string const& tokenLower) {
    return stopwords().contains(tokenLower);
}

// Light stemming: trims one common EN/ES suffix (first rule that matches) to avoid over-stemming.
// Covers ~80% of the variants users type in short queries; not a full Porter stemmer.
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

    add(token);
    add(stem(token));

    // synonym (if any)
    auto const& syn = synonyms();
    auto it = syn.find(token);
    if (it != syn.end()) {
        add(it->second);
        add(stem(it->second));
    }

    // Also try the stem as a synonym key (e.g. "musicas" -> "musica" -> "music").
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
