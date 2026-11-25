#include "LightLemmatizer.hpp"

#include <algorithm>
#include <cctype>

namespace paimon::guide {

namespace {

// minimal "accented -> plain letter" map to normalize user queries (common es/pt/fr, utf-8 0xc3 0xxx).
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

} // namespace

// shared normalized en/es stopwords.

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
        // keep topic-bearing registry aliases out of this generic list.
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

// canonical synonym map.

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
        // ---- round 10: conversational + gd shorthand (helps short queries) ----
        {"ola",        "hola"},
        {"holas",      "hola"},
        {"hello",      "hi"},
        {"hey",        "hi"},
        {"okay",       "ok"},
        {"oke",        "ok"},
        {"vale",       "ok"},
        {"ok",         "ok"},
        {"grax",       "gracias"},
        {"gracia",     "gracias"},
        {"thanku",     "thanks"},
        {"thankyou",   "thanks"},
        {"chao",       "adios"},
        {"bye",        "adios"},
        {"pantalla",   "background"},
        {"screen",     "background"},
        {"flecha",     "cursor"},
        {"arrow",      "cursor"},
        {"melodia",    "music"},
        {"melody",     "music"},
        {"cancioncita","music"},
        {"fotito",     "picture"},
        {"fotillo",    "picture"},
        {"cap",        "capture"},
        {"capturar",   "capture"},
        {"foto",       "picture"},
        {"fotos",      "picture"},
        {"imagen",     "picture"},
        {"imagenes",   "picture"},
        {"deamon",     "demon"},
        {"demons",     "demon"},
        {"lvl",        "level"},
        {"niv",        "level"},
        {"nivel",      "level"},
        {"levels",     "level"},
        {"niveles",    "level"},
        {"ajuste",     "settings"},
        {"ajustes",    "settings"},
        {"configuracion", "settings"},
        {"opciones",   "settings"},
        {"option",     "settings"},
        {"options",    "settings"},
        {"personalizar","custom"},
        {"customizar", "custom"},
        {"customize",  "custom"},
        {"customise",  "custom"},
        {"cambiar",    "change"},
        {"cambia",     "change"},
        {"cambio",     "change"},
        {"pon",        "put"},
        {"ponme",      "put"},
        {"quita",      "remove"},
        {"saca",       "remove"},
        {"muestrame",   "show"},
        {"ensename",   "show"},
        {"dime",       "tell"},
        {"explicame",  "explain"},
        {"explain",    "explain"},
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

// trim one common en/es suffix; this is intentionally lighter than a full stemmer.
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
    // spanish participles: configurado -> configur, personalizada -> personaliz
    if (t.size() >= 6 && (endsWith("ados") || endsWith("idos")
                       || endsWith("adas") || endsWith("idas"))) {
        return t.substr(0, t.size() - 4);
    }
    if (t.size() >= 5 && (endsWith("ado") || endsWith("ido")
                       || endsWith("ada") || endsWith("ida"))) {
        return t.substr(0, t.size() - 3);
    }
    // spanish infinitives (only len>=6 so nouns like "lugar" keep their root).
    if (t.size() >= 6 && (endsWith("ar") || endsWith("er") || endsWith("ir"))) {
        return t.substr(0, t.size() - 2);
    }
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

    // also resolve the stem as a synonym key.
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

std::string LightLemmatizer::normalize(std::string s) {
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

std::vector<std::string> LightLemmatizer::tokenize(std::string const& normalized) {
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

}
