#include "LightLemmatizer.hpp"

#include <algorithm>

namespace paimon::guide {

// Shared normalized EN/ES stopwords.

std::unordered_set<std::string> const& LightLemmatizer::stopwords() {
    static const std::unordered_set<std::string> kStopwords = {
        "the", "a", "an", "is", "are", "be", "to", "of", "in", "on",
        "at", "by", "for", "with", "and", "or", "but", "i", "you",
        "we", "it", "this", "that", "those", "these", "do", "does",
        "did", "can", "should", "would", "could", "will", "shall",
        "what", "where", "when", "how", "who", "why", "which",
        "el", "la", "los", "las", "un", "una", "unos", "unas",
        "de", "del", "al", "a", "en", "con", "por", "para",
        "sin", "sobre", "y", "o", "u", "e", "ni", "pero", "yo",
        "tu", "el", "ella", "nosotros", "vosotros", "ellos", "ellas",
        "es", "esta", "estan", "ser", "estar", "esto", "eso", "aquello",
        "ese", "esa", "este", "esa", "aquel",
        "donde", "que", "quien", "cuando", "como", "porque", "cual",
        "cuanto", "cuantos", "cuanta", "cuantas",
        "hay", "tiene", "tengo", "tienes", "puedo", "puede", "puedes",
        "sabes", "quiero", "quieres", "necesito", "ayuda",
        // Keep topic-bearing registry aliases out of this generic list.
        "configure", "change", "set", "enable", "disable", "open", "find",
        "use", "want", "need", "show", "make", "give", "tell",
        "configurar", "cambiar", "poner", "activar", "desactivar", "abrir",
        "encontrar", "usar", "mostrar", "dame", "dime", "hacer",
        "please", "me", "my", "your", "any", "stuff", "things", "thing",
        "porfavor", "por favor", "mi", "tu", "su", "alguno", "algun",
        "cosas", "cosa",
    };
    return kStopwords;
}

// Canonical synonym map.

std::unordered_map<std::string, std::string> const& LightLemmatizer::synonyms() {
    static const std::unordered_map<std::string, std::string> kSyn = {
        {"pic",     "picture"},
        {"pfp",     "profile"},
        {"avatar",  "profile"},
        {"bg",      "background"},
        {"rpc",     "discord"},
        {"sfx",     "audio"},
        {"qh",      "quickhub"},
        {"thumb",   "thumbnail"},
        {"thumbs",  "thumbnail"},
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
        {"perfilar",   "perfil"},
        {"perfiles",   "perfil"},
        {"fondos",     "fondo"},
        {"fondo",      "background"}, // unify cross-lingual matching
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
        {"ayuda",      "help"},   // stopword, kept for completeness
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
        {"papel",      "background"},
        {"tapiz",      "background"},
        {"companero",  "pet"},
        {"rueda",      "quickhub"},
        {"radial",     "quickhub"},
        {"playlists",  "playlist"},
        {"barra",      "progressbar"},
        {"blur",       "background"},
        {"blurry",     "background"},
        {"lag",        "smooth"},
        {"laggy",      "smooth"},
        {"fps",        "smooth"},
        {"jerky",      "smooth"},
        {"recolor",    "icons"},
        {"recolour",   "icons"},
        {"icon",       "icons"},
        {"rainbow",    "icons"},
        {"scrolling",  "scroll"},
        {"screenshot", "capture"},
        {"snap",       "capture"},
        {"previews",   "thumbnail"},
        {"collab",     "collab"},
        {"collaboration", "collab"},
        {"multiplayer","collab"},
        {"undo",       "history"},
        {"redo",       "history"},
        {"history",    "history"},
        {"rotate",     "rotate"},
        {"rotation",   "rotate"},
        {"filter",     "filters"},
        {"filters",    "filters"},
        {"physics",    "physics"},
        {"mention",    "mentions"},
        {"mentions",   "mentions"},
        {"notification","notifications"},
        {"notifications","notifications"},
        {"dm",         "messages"},
        {"messages",   "messages"},
        {"inbox",      "messages"},
        {"texture",    "textures"},
        {"textures",   "textures"},
        {"sprite",     "textures"},
        {"pack",       "textures"},
        {"slider",     "slider"},
        {"deslizador", "slider"},
        {"leaderboard","ranking"},
        {"leaderboards","ranking"},
        {"ranking",    "ranking"},
        {"layout",     "layout"},
        {"typography", "fonts"},
        {"font",       "fonts"},
        {"fonts",      "fonts"},
        {"canvas",     "paidraw"},
        {"drawing",    "paidraw"},
        {"draw",       "paidraw"},
        {"borroso",    "background"},
        {"borrosa",    "background"},
        {"desenfoque", "background"},
        {"lentitud",   "smooth"},
        {"lento",      "smooth"},
        {"entrecortado","smooth"},
        {"iconos",     "icons"},
        {"icono",      "icons"},
        {"recolorear", "icons"},
        {"arcoiris",   "icons"},
        {"desplazamiento", "scroll"},
        {"capturadora","capture"},
        {"rotas",      "broken"},
        {"rotos",      "broken"},
        {"colaboracion","collab"},
        {"multijugador","collab"},
        {"amigos",     "collab"},
        {"amigo",      "collab"},
        {"friends",    "collab"},
        {"friend",     "collab"},
        {"edito",      "editor"},
        {"editar",     "editor"},
        {"deshacer",   "history"},
        {"rehacer",    "history"},
        {"historial",  "history"},
        {"rotar",      "rotate"},
        {"rotacion",   "rotate"},
        {"filtro",     "filters"},
        {"filtros",    "filters"},
        {"fisica",     "physics"},
        {"mencion",    "mentions"},
        {"menciones",  "mentions"},
        {"notificacion","notifications"},
        {"notificaciones","notifications"},
        {"mensajes",   "messages"},
        {"texturas",   "textures"},
        {"clasificacion","ranking"},
        {"tipografia", "fonts"},
        {"fuentes",    "fonts"},
        {"fuente",     "fonts"},
        {"dibujar",    "paidraw"},
        {"dibujo",     "paidraw"},
        {"lienzo",     "paidraw"},
        {"iconmaker",  "iconmaker"},
        {"creador",    "iconmaker"},
        {"creator",    "iconmaker"},
        {"galeria",    "gallery"},
        {"galery",     "gallery"},
        {"store",      "gallery"},
        {"tienda",     "gallery"},
        {"shop",       "gallery"},
        {"gradient",   "gradients"},
        {"gradients",  "gradients"},
        {"degradado",  "gradients"},
        {"degradados", "gradients"},
        {"dual",       "separatedual"},
        {"p2",         "separatedual"},
        {"separado",   "separatedual"},
        {"separate",   "separatedual"},
        {"gold",       "golden"},
        {"golden",     "golden"},
        {"record",     "golden"},
        {"dorado",     "golden"},
        {"death",      "deatheffects"},
        {"deaths",     "deatheffects"},
        {"explosion",  "deatheffects"},
        {"explode",    "deatheffects"},
        {"muerte",     "deatheffects"},
        {"morir",      "deatheffects"},
        {"performance","performance"},
        {"optimize",   "performance"},
        {"optimizar",  "performance"},
        {"rendimiento","performance"},
        {"copy",       "iconcopy"},
        {"copiar",     "iconcopy"},
        {"sets",       "iconsets"},
        {"estilos",    "iconsets"},
        {"request",    "requests"},
        {"requests",   "requests"},
        {"queue",      "requests"},
        {"cola",       "requests"},
        {"pedidos",    "requests"},
        {"twitch",     "requests"},
        {"stream",     "requests"},
        {"loudness",   "dynamicvolume"},
        {"loud",       "dynamicvolume"},
        {"sonoridad",  "dynamicvolume"},
        {"loop",       "menuloop"},
        {"nowplaying", "menuloop"},
        {"stats",      "infosuite"},
        {"statistics", "infosuite"},
        {"statistic",  "infosuite"},
        {"heatmap",    "infosuite"},
    };
    return kSyn;
}

bool LightLemmatizer::isStopword(std::string const& tokenLower) {
    return stopwords().contains(tokenLower);
}

// Trim one common EN/ES suffix; this is intentionally lighter than a full stemmer.
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

    auto const& syn = synonyms();
    auto it = syn.find(token);
    if (it != syn.end()) {
        add(it->second);
        add(stem(it->second));
    }

    // Also resolve the stem as a synonym key.
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

}
