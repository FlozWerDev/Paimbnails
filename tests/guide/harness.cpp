// Host-side test harness for Paimon's matcher (PaigoritV1 + LightLemmatizer).
// Compiles the real algorithm sources directly (unity build) and runs a labeled
// query set, reporting accuracy per category. This is the loop that lets us tune
// the algorithm with evidence instead of guessing.
//
// Build: tests/guide/run_tests.bat

#include "../../src/features/guide/services/LightLemmatizer.cpp"
#include "../../src/features/guide/services/PaigoritV1.cpp"
#include "TestIntents.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

using namespace paimon::guide;

// Mirror of PaimonGuideService::stripBasicAccents + normalize + tokenize, so the
// harness feeds PaigoritV1 exactly what the service would.
namespace {

std::string stripBasicAccents(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char c2 = static_cast<unsigned char>(in[i + 1]);
            char r = 0;
            switch (c2) {
                case 0xA1: case 0xA0: case 0xA2: case 0xA3: case 0xA4: case 0xA5: r='a'; break;
                case 0xA9: case 0xA8: case 0xAA: case 0xAB: r='e'; break;
                case 0xAD: case 0xAC: case 0xAE: case 0xAF: r='i'; break;
                case 0xB3: case 0xB2: case 0xB4: case 0xB5: case 0xB6: r='o'; break;
                case 0xBA: case 0xB9: case 0xBB: case 0xBC: r='u'; break;
                case 0xB1: r='n'; break;
                case 0x81: case 0x80: case 0x82: case 0x83: case 0x84: case 0x85: r='a'; break;
                case 0x89: case 0x88: case 0x8A: case 0x8B: r='e'; break;
                case 0x8D: case 0x8C: case 0x8E: case 0x8F: r='i'; break;
                case 0x93: case 0x92: case 0x94: case 0x95: case 0x96: r='o'; break;
                case 0x9A: case 0x99: case 0x9B: case 0x9C: r='u'; break;
                case 0x91: r='n'; break;
                default: break;
            }
            if (r != 0) { out.push_back(r); i += 2; continue; }
        }
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

std::string normalize(std::string s) {
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
            } else if (!lastSpace) {
                out.push_back(' ');
                lastSpace = true;
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> tokenize(std::string const& n) {
    std::vector<std::string> t;
    std::string cur;
    for (char c : n) {
        if (c == ' ') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

struct Case {
    std::string query;
    std::string lang;                 // "english" / "spanish"
    std::vector<std::string> expect;  // acceptable intent ids; empty => expect fallback
    std::string group;
};

const char* FALLBACK = "<fallback>";

} // namespace

int main() {
    auto intents = test::makeIntents();

    std::vector<Case> cases = {
        // --- exact display names (EN) ---
        {"custom cursor", "english", {"custom-cursor"}, "exact"},
        {"menu music", "english", {"menu-music"}, "exact"},
        {"discord rich presence", "english", {"discord-rich-presence"}, "exact"},
        {"quick hub", "english", {"quick-hub"}, "exact"},
        {"thumbnail settings", "english", {"thumbnail-settings"}, "exact"},
        {"profile background", "english", {"profile-background"}, "exact"},
        {"custom progress bar", "english", {"progress-bar"}, "exact"},

        // --- single-word aliases ---
        {"cursor", "english", {"custom-cursor"}, "alias"},
        {"discord", "english", {"discord-rich-presence"}, "alias"},
        {"rpc", "english", {"discord-rich-presence"}, "alias"},
        {"thumbnails", "english", {"thumbnail-settings"}, "alias"},
        {"pet", "english", {"pet"}, "alias"},
        {"mascot", "english", {"pet"}, "alias"},
        {"volume", "english", {"scroll-keybinds"}, "alias"},
        {"shaders", "english", {"extra-effects"}, "alias"},
        {"forum", "english", {"hub"}, "alias"},
        {"qh", "english", {"quick-hub"}, "alias"},

        // --- synonyms / variants (lemmatizer) ---
        {"songs", "english", {"menu-music", "music-library", "music-playlists"}, "synonym"},
        {"wallpaper", "english", {"scene-background", "profile-background"}, "synonym"},
        {"mouse", "english", {"custom-cursor"}, "synonym"},
        {"avatar", "english", {"profile-photo-editor"}, "synonym"},
        {"soundtrack", "english", {"menu-music", "music-library", "music-playlists"}, "synonym"},
        {"companion", "english", {"pet"}, "synonym"},

        // --- natural language phrasing (EN) ---
        {"where do i configure the cursor", "english", {"custom-cursor"}, "natural"},
        {"how do i set menu music", "english", {"menu-music"}, "natural"},
        {"i want to change my profile picture", "english", {"profile-photo-editor"}, "natural"},
        {"how can i enable discord rich presence", "english", {"discord-rich-presence"}, "natural"},
        {"where are the thumbnail settings", "english", {"thumbnail-settings"}, "natural"},
        {"change the menu background", "english", {"scene-background"}, "natural"},

        // --- typos ---
        {"cursr", "english", {"custom-cursor"}, "typo"},
        {"discrod", "english", {"discord-rich-presence"}, "typo"},
        {"thmbnails", "english", {"thumbnail-settings"}, "typo"},
        {"musci", "english", {"menu-music", "music-library", "music-playlists"}, "typo"},

        // --- conversational ---
        {"hello", "english", {"greeting"}, "conv"},
        {"hi there", "english", {"greeting"}, "conv"},
        {"thank you", "english", {"thanks"}, "conv"},
        {"who are you", "english", {"who-are-you"}, "conv"},
        {"tell me a joke", "english", {"joke"}, "conv"},
        {"what can you do", "english", {"what-can-you-do", "help-general"}, "conv"},
        {"bye", "english", {"goodbye"}, "conv"},
        {"how are you", "english", {"how-are-you"}, "conv"},

        // --- non-matches (should fall back, NOT a false positive) ---
        {"asdfghjkl", "english", {FALLBACK}, "nomatch"},
        {"buy me a pizza", "english", {FALLBACK}, "nomatch"},
        {"the weather is nice today", "english", {FALLBACK}, "nomatch"},
        {"123456", "english", {FALLBACK}, "nomatch"},

        // --- Spanish: aliases / names ---
        {"cursor", "spanish", {"custom-cursor"}, "es-alias"},
        {"raton", "spanish", {"custom-cursor"}, "es-synonym"},
        {"fondos", "spanish", {"scene-background"}, "es-alias"},
        {"musica del menu", "spanish", {"menu-music"}, "es-exact"},
        {"miniaturas", "spanish", {"thumbnail-settings"}, "es-alias"},
        {"mascota", "spanish", {"pet"}, "es-alias"},
        {"volumen", "spanish", {"scroll-keybinds"}, "es-alias"},
        {"vinilo", "spanish", {"menu-music"}, "es-synonym"},

        // --- Spanish: natural phrasing ---
        {"donde configuro el cursor", "spanish", {"custom-cursor"}, "es-natural"},
        {"como pongo musica de menu", "spanish", {"menu-music"}, "es-natural"},
        {"donde cambio los fondos", "spanish", {"scene-background"}, "es-natural"},
        {"quiero cambiar mi foto de perfil", "spanish", {"profile-photo-editor"}, "es-natural"},
        {"como activo el discord", "spanish", {"discord-rich-presence"}, "es-natural"},

        // --- Spanish: conversational ---
        {"hola", "spanish", {"greeting"}, "es-conv"},
        {"gracias", "spanish", {"thanks"}, "es-conv"},
        {"quien eres", "spanish", {"who-are-you"}, "es-conv"},
        {"cuentame un chiste", "spanish", {"joke"}, "es-conv"},
        {"adios", "spanish", {"goodbye"}, "es-conv"},

        // --- Spanish: non-match ---
        {"comprame una pizza", "spanish", {FALLBACK}, "es-nomatch"},

        // ===== Hardening round 2 =====
        // category lead: bare "music" should land on the main music popup
        {"music", "english", {"menu-music"}, "category"},
        {"playlist", "english", {"music-playlists"}, "alias2"},
        {"library", "english", {"music-library"}, "alias2"},
        {"badge", "english", {"custom-badge"}, "alias2"},
        {"reviews", "english", {"profile-reviews"}, "alias2"},
        {"language", "english", {"geode-settings"}, "alias2"},
        {"update", "english", {"mod-updates"}, "alias2"},
        {"cache", "english", {"paiconfig"}, "alias2"},
        {"effects", "english", {"extra-effects"}, "alias2"},
        {"transitions", "english", {"transition-settings"}, "alias2"},
        {"for you", "english", {"foryou-preferences"}, "alias2"},
        {"progress bar", "english", {"progress-bar"}, "alias2"},

        // compound disambiguation: more specific compound beats the category lead
        {"profile music", "english", {"profile-music"}, "compound"},
        {"comment background", "english", {"comment-background"}, "compound"},
        {"profile picture", "english", {"profile-photo-editor"}, "compound"},

        // case-insensitivity
        {"CURSOR", "english", {"custom-cursor"}, "case"},
        {"Discord Rich Presence", "english", {"discord-rich-presence"}, "case"},

        // accented Spanish (exercises the accent stripper)
        {"m\xC3\xBAsica", "spanish", {"menu-music"}, "es-accent"},
        {"configuraci\xC3\xB3n de perfil", "spanish", {"profile-settings"}, "es-accent"},
        {"rese\xC3\xB1" "as", "spanish", {"profile-reviews"}, "es-accent"},
        {"idioma", "spanish", {"geode-settings"}, "es-alias2"},

        // clear non-matches (precision)
        {"i want to play geometry dash", "english", {FALLBACK}, "nomatch2"},
        {"delete my account", "english", {FALLBACK}, "nomatch2"},
        {"random gibberish words here", "english", {FALLBACK}, "nomatch2"},

        // ===== Hardening round 3 =====
        // "background" alone is the scene one, not the profile one
        {"background", "english", {"scene-background"}, "precision"},
        {"fondo", "spanish", {"scene-background"}, "precision"},
        {"screenshot", "english", {"capture"}, "alias3"},
        {"rich presence", "english", {"discord-rich-presence"}, "alias3"},
        {"wheel menu", "english", {"quick-hub"}, "alias3"},
        {"loading bar", "english", {"progress-bar"}, "alias3"},
        {"my songs", "english", {"music-library"}, "alias3"},
        {"news", "english", {"hub"}, "alias3"},
        {"community", "english", {"hub"}, "alias3"},
        {"HOLA", "spanish", {"greeting"}, "case-es"},
        {"i need a custom cursor please", "english", {"custom-cursor"}, "natural2"},
        {"what time is it", "english", {FALLBACK}, "nomatch3"},
        {"open the door", "english", {FALLBACK}, "nomatch3"},

        // ===== Round 4: extensive per-intent coverage (EN) =====
        {"profile background", "english", {"profile-background"}, "cov-en"},
        {"profile wallpaper", "english", {"profile-background"}, "cov-en"},
        {"profile photo editor", "english", {"profile-photo-editor"}, "cov-en"},
        {"profile pic", "english", {"profile-photo-editor"}, "cov-en"},
        {"profile settings", "english", {"profile-settings"}, "cov-en"},
        {"profile config", "english", {"profile-settings"}, "cov-en"},
        {"profile song", "english", {"profile-music"}, "cov-en"},
        {"comments bg", "english", {"comment-background"}, "cov-en"},
        {"custom badge", "english", {"custom-badge"}, "cov-en"},
        {"user badge", "english", {"custom-badge"}, "cov-en"},
        {"ratings", "english", {"profile-reviews"}, "cov-en"},
        {"who viewed", "english", {"profile-views"}, "cov-en"},
        {"visitors", "english", {"profile-views"}, "cov-en"},
        {"scene background", "english", {"scene-background"}, "cov-en"},
        {"menu background", "english", {"scene-background"}, "cov-en"},
        {"music library", "english", {"music-library"}, "cov-en"},
        {"music playlists", "english", {"music-playlists"}, "cov-en"},
        {"vinyl", "english", {"menu-music"}, "cov-en"},
        {"menuloop", "english", {"menu-loop", "menu-music"}, "cov-en"},
        {"main menu music", "english", {"menu-music"}, "cov-en"},
        {"pointer", "english", {"custom-cursor"}, "cov-en"},
        {"mouse pointer", "english", {"custom-cursor"}, "cov-en"},
        {"status", "english", {"discord-rich-presence"}, "cov-en"},
        {"rich presence", "english", {"discord-rich-presence"}, "cov-en"},
        {"mascot", "english", {"pet"}, "cov-en"},
        {"fish", "english", {"pet"}, "cov-en"},
        {"transition settings", "english", {"transition-settings"}, "cov-en"},
        {"scene transition", "english", {"transition-settings"}, "cov-en"},
        {"extra effects", "english", {"extra-effects"}, "cov-en"},
        {"visual effects", "english", {"extra-effects"}, "cov-en"},
        {"fx", "english", {"extra-effects"}, "cov-en"},
        {"progressbar", "english", {"progress-bar"}, "cov-en"},
        {"radial menu", "english", {"quick-hub"}, "cov-en"},
        {"shortcut wheel", "english", {"quick-hub"}, "cov-en"},
        {"thumbnail order", "english", {"thumbnail-order"}, "cov-en"},
        {"sort thumbnails", "english", {"thumbnail-order"}, "cov-en"},
        {"level cell", "english", {"level-cell-settings"}, "cov-en"},
        {"scroll keybinds", "english", {"scroll-keybinds"}, "cov-en"},
        {"keybinds", "english", {"scroll-keybinds"}, "cov-en"},
        {"for you preferences", "english", {"foryou-preferences"}, "cov-en"},
        {"feed", "english", {"foryou-preferences"}, "cov-en"},
        {"recommendations", "english", {"foryou-preferences"}, "cov-en"},
        {"paiconfig", "english", {"paiconfig"}, "cov-en"},
        {"extras", "english", {"paiconfig"}, "cov-en"},
        {"clear cache", "english", {"paiconfig"}, "cov-en"},
        {"config", "english", {"paiconfig"}, "cov-en"},
        {"settings", "english", {"paiconfig", "geode-settings"}, "cov-en"},
        {"paimon hub", "english", {"hub"}, "cov-en"},
        {"forum", "english", {"hub"}, "cov-en"},
        {"posts", "english", {"hub"}, "cov-en"},
        {"mod settings", "english", {"geode-settings"}, "cov-en"},
        {"preferences", "english", {"geode-settings"}, "cov-en"},
        {"options", "english", {"geode-settings", "help-general"}, "cov-en"},
        {"snap", "english", {"capture"}, "cov-en"},
        {"thumbnail capture", "english", {"capture"}, "cov-en"},

        // ===== Round 4: extensive per-intent coverage (ES) =====
        {"fondo de perfil", "spanish", {"profile-background"}, "cov-es"},
        {"editor de foto de perfil", "spanish", {"profile-photo-editor"}, "cov-es"},
        {"imagen de perfil", "spanish", {"profile-photo-editor"}, "cov-es"},
        {"ajustes de perfil", "spanish", {"profile-settings"}, "cov-es"},
        {"cancion de perfil", "spanish", {"profile-music"}, "cov-es"},
        {"fondo de comentarios", "spanish", {"comment-background"}, "cov-es"},
        {"badge personalizado", "spanish", {"custom-badge"}, "cov-es"},
        {"insignia", "spanish", {"custom-badge"}, "cov-es"},
        {"valoraciones", "spanish", {"profile-reviews"}, "cov-es"},
        {"visitas de perfil", "spanish", {"profile-views"}, "cov-es"},
        {"fondo de escena", "spanish", {"scene-background"}, "cov-es"},
        {"escenario", "spanish", {"scene-background"}, "cov-es"},
        {"biblioteca de musica", "spanish", {"music-library"}, "cov-es"},
        {"playlists de musica", "spanish", {"music-playlists"}, "cov-es"},
        {"puntero", "spanish", {"custom-cursor"}, "cov-es"},
        {"presencia", "spanish", {"discord-rich-presence"}, "cov-es"},
        {"estado", "spanish", {"discord-rich-presence"}, "cov-es"},
        {"pez", "spanish", {"pet"}, "cov-es"},
        {"transiciones", "spanish", {"transition-settings"}, "cov-es"},
        {"efectos", "spanish", {"extra-effects"}, "cov-es"},
        {"barra de progreso", "spanish", {"progress-bar"}, "cov-es"},
        {"menu radial", "spanish", {"quick-hub"}, "cov-es"},
        {"orden de miniaturas", "spanish", {"thumbnail-order"}, "cov-es"},
        {"lista de niveles", "spanish", {"level-cell-settings"}, "cov-es"},
        {"atajos de teclado", "spanish", {"scroll-keybinds"}, "cov-es"},
        {"subir volumen", "spanish", {"scroll-keybinds"}, "cov-es"},
        {"para ti", "spanish", {"foryou-preferences"}, "cov-es"},
        {"recomendaciones", "spanish", {"foryou-preferences"}, "cov-es"},
        {"limpiar cache", "spanish", {"paiconfig"}, "cov-es"},
        {"foro", "spanish", {"hub"}, "cov-es"},
        {"comunidad", "spanish", {"hub"}, "cov-es"},
        {"noticias", "spanish", {"hub"}, "cov-es"},
        {"ajustes del mod", "spanish", {"geode-settings"}, "cov-es"},
        {"idioma", "spanish", {"geode-settings"}, "cov-es"},
        {"version", "spanish", {"mod-updates"}, "cov-es"},
        {"actualizar", "spanish", {"mod-updates"}, "cov-es"},

        // ===== Round 4: more typos (single-edit, longer words) =====
        {"backgound", "english", {"scene-background"}, "typo2"},
        {"transtion", "english", {"transition-settings"}, "typo2"},
        {"playlst", "english", {"music-playlists"}, "typo2"},
        {"thumbnial", "english", {"thumbnail-settings"}, "typo2"},
        {"progres bar", "english", {"progress-bar"}, "typo2"},
        {"discrd", "english", {"discord-rich-presence"}, "typo2"},

        // ===== Round 4: natural language (EN) =====
        {"how do i change my cursor", "english", {"custom-cursor"}, "nat-en"},
        {"i want to set up discord rich presence", "english", {"discord-rich-presence"}, "nat-en"},
        {"where can i find the menu music", "english", {"menu-music"}, "nat-en"},
        {"how to customize the progress bar", "english", {"progress-bar"}, "nat-en"},
        {"i need to clear the cache", "english", {"paiconfig"}, "nat-en"},
        {"how do i change the language", "english", {"geode-settings"}, "nat-en"},
        {"show me the forum", "english", {"hub"}, "nat-en"},
        {"how do i use the pet", "english", {"pet"}, "nat-en"},
        {"where are my profile views", "english", {"profile-views"}, "nat-en"},

        // ===== Round 4: natural language (ES) =====
        {"como cambio el cursor", "spanish", {"custom-cursor"}, "nat-es"},
        {"donde esta la musica del menu", "spanish", {"menu-music"}, "nat-es"},
        {"quiero personalizar mi cursor", "spanish", {"custom-cursor"}, "nat-es"},
        {"como activo discord", "spanish", {"discord-rich-presence"}, "nat-es"},
        {"donde veo mis visitas de perfil", "spanish", {"profile-views"}, "nat-es"},
        {"donde configuro las miniaturas", "spanish", {"thumbnail-settings"}, "nat-es"},
        {"quiero un chiste", "spanish", {"joke"}, "nat-es"},
        {"muchas gracias paimon", "spanish", {"thanks"}, "nat-es"},

        // ===== Round 4: conversational coverage =====
        {"help", "english", {"help-general"}, "conv2"},
        {"guide", "english", {"help-general"}, "conv2"},
        {"tutorial", "english", {"help-general"}, "conv2"},
        {"ayuda", "spanish", {"help-general"}, "conv2"},
        {"your name", "english", {"who-are-you"}, "conv2"},
        {"what are you", "english", {"who-are-you"}, "conv2"},
        {"como te llamas", "spanish", {"who-are-you"}, "conv2"},
        {"thx", "english", {"thanks"}, "conv2"},
        {"ty", "english", {"thanks"}, "conv2"},
        {"te agradezco", "spanish", {"thanks"}, "conv2"},
        {"hey", "english", {"greeting"}, "conv2"},
        {"good morning", "english", {"greeting"}, "conv2"},
        {"buenas", "spanish", {"greeting"}, "conv2"},
        {"buenos dias", "spanish", {"greeting"}, "conv2"},
        {"saludos", "spanish", {"greeting"}, "conv2"},
        {"como estas", "spanish", {"how-are-you"}, "conv2"},
        {"estas bien", "spanish", {"how-are-you"}, "conv2"},
        {"i love you", "english", {"compliment"}, "conv2"},
        {"you are awesome", "english", {"compliment"}, "conv2"},
        {"te amo", "spanish", {"compliment"}, "conv2"},
        {"eres genial", "spanish", {"compliment"}, "conv2"},
        {"goodbye", "english", {"goodbye"}, "conv2"},
        {"see you", "english", {"goodbye"}, "conv2"},
        {"hasta luego", "spanish", {"goodbye"}, "conv2"},
        {"chao", "spanish", {"goodbye"}, "conv2"},
        {"joke", "english", {"joke"}, "conv2"},
        {"make me laugh", "english", {"joke"}, "conv2"},
        {"chiste", "spanish", {"joke"}, "conv2"},
        {"que puedes hacer", "spanish", {"what-can-you-do", "help-general"}, "conv2"},
        {"your features", "english", {"what-can-you-do"}, "conv2"},

        // ===== Round 4: punctuation / whitespace / case =====
        {"Cursor!", "english", {"custom-cursor"}, "fmt"},
        {"discord?", "english", {"discord-rich-presence"}, "fmt"},
        {"MENU MUSIC", "english", {"menu-music"}, "fmt"},
        {"  cursor  ", "english", {"custom-cursor"}, "fmt"},
        {"quick-hub", "english", {"quick-hub"}, "fmt"},

        // ===== Round 4: more non-matches =====
        {"i hate this game", "english", {FALLBACK}, "nomatch4"},
        {"lorem ipsum dolor", "english", {FALLBACK}, "nomatch4"},
        {"zzzzzz xxxxxx", "english", {FALLBACK}, "nomatch4"},
        {"el clima esta feo hoy", "spanish", {FALLBACK}, "nomatch4"},

        // ===== Round 5: newly added features =====
        {"smooth scroll", "english", {"smooth-scroll"}, "newfeat"},
        {"scroll suave", "spanish", {"smooth-scroll"}, "newfeat"},
        {"slider", "english", {"custom-slider"}, "newfeat"},
        {"custom slider", "english", {"custom-slider"}, "newfeat"},
        {"beat shaders", "english", {"beat-shaders"}, "newfeat"},
        {"audio shaders", "english", {"beat-shaders"}, "newfeat"},
        {"score cell", "english", {"score-cell"}, "newfeat"},
        {"texture studio", "english", {"texture-studio"}, "newfeat"},
        {"texture pack", "english", {"texture-studio"}, "newfeat"},
        {"sprite editor", "english", {"texture-studio"}, "newfeat"},
        {"colorful icons", "english", {"colorful-icons"}, "newfeat"},
        {"recolor icons", "english", {"colorful-icons"}, "newfeat"},
        {"iconos paimon", "spanish", {"colorful-icons"}, "newfeat"},
        {"capture", "english", {"capture"}, "newfeat"},
        {"take screenshot", "english", {"capture"}, "newfeat"},
        {"capturadora", "spanish", {"capture"}, "newfeat"},
        {"leaderboard", "english", {"leaderboards"}, "newfeat"},
        {"ranking", "english", {"leaderboards"}, "newfeat"},
        {"clasificacion", "spanish", {"leaderboards"}, "newfeat"},
        {"top creators", "english", {"leaderboards"}, "newfeat"},
        {"update the mod", "english", {"mod-updates"}, "newfeat"},
        {"check for updates", "english", {"mod-updates"}, "newfeat"},
        {"buscar actualizaciones", "spanish", {"mod-updates"}, "newfeat"},
        {"profile redesign", "english", {"profile-redesign"}, "newfeat"},
        {"redesign profile", "english", {"profile-redesign"}, "newfeat"},
        {"global icons", "english", {"global-icons"}, "newfeat"},
        {"emotes", "english", {"emotes"}, "newfeat"},
        {"stickers", "english", {"emotes"}, "newfeat"},
        {"emoji", "spanish", {"emotes"}, "newfeat"},
        {"custom fonts", "english", {"fonts"}, "newfeat"},
        {"typography", "english", {"fonts"}, "newfeat"},
        {"fuentes", "spanish", {"fonts"}, "newfeat"},
        {"paidraw", "english", {"paidraw"}, "newfeat"},
        {"drawing", "english", {"paidraw"}, "newfeat"},
        {"layout editor", "english", {"layout-editor"}, "newfeat"},
        {"menu layout", "english", {"layout-editor"}, "newfeat"},
        {"mover botones", "spanish", {"layout-editor"}, "newfeat"},
        {"search history", "english", {"search-history"}, "newfeat"},
        {"incognito", "english", {"search-history"}, "newfeat"},
        {"auto preview", "english", {"auto-preview"}, "newfeat"},
        {"generate thumbnail", "english", {"auto-preview"}, "newfeat"},
        {"dynamic song", "english", {"dynamic-song"}, "newfeat"},
        {"editor music", "english", {"editor-music"}, "newfeat"},
        {"musica del editor", "spanish", {"editor-music"}, "newfeat"},
        {"level info background", "english", {"level-info-background"}, "newfeat"},

        // natural-language phrasing for new features
        {"how do i make scrolling smooth", "english", {"smooth-scroll"}, "newfeat-nat"},
        {"i want to recolor my icons", "english", {"colorful-icons"}, "newfeat-nat"},
        {"how do i take a screenshot of my level", "english", {"capture"}, "newfeat-nat"},
        {"como actualizo el mod", "spanish", {"mod-updates"}, "newfeat-nat"},
        {"where is the texture pack editor", "english", {"texture-studio"}, "newfeat-nat"},

        // ===== Round 6: mod-knowledge (the "small AI" info layer) =====
        {"what is paimbnails", "english", {"mod-about"}, "knowledge"},
        {"about the mod", "english", {"mod-about"}, "knowledge"},
        {"what does this mod do", "english", {"mod-about"}, "knowledge"},
        {"que es paimbnails", "spanish", {"mod-about"}, "knowledge"},
        {"sobre el mod", "spanish", {"mod-about"}, "knowledge"},
        {"who made paimbnails", "english", {"mod-author"}, "knowledge"},
        {"who is the developer", "english", {"mod-author"}, "knowledge"},
        {"creator", "english", {"mod-author"}, "knowledge"},
        {"quien hizo paimbnails", "spanish", {"mod-author"}, "knowledge"},
        {"is it free", "english", {"mod-free"}, "knowledge"},
        {"how much does it cost", "english", {"mod-free"}, "knowledge"},
        {"es gratis", "spanish", {"mod-free"}, "knowledge"},
        {"how do i install paimbnails", "english", {"mod-install"}, "knowledge"},
        {"como se instala", "spanish", {"mod-install"}, "knowledge"},
        {"list of features", "english", {"feature-list"}, "knowledge"},
        {"what features does it have", "english", {"feature-list"}, "knowledge"},
        {"todas las funciones", "spanish", {"feature-list"}, "knowledge"},
        {"support", "english", {"mod-support"}, "knowledge"},
        {"report a bug", "english", {"mod-support"}, "knowledge"},
        {"soporte", "spanish", {"mod-support"}, "knowledge"},
        // ensure mod-knowledge doesn't steal feature queries
        {"who are you", "english", {"who-are-you"}, "knowledge"},
        {"paimbnails cursor", "english", {"custom-cursor"}, "knowledge"},

        // ===== Round 7: problem / search phrases (soft NLU) =====
        {"thumbnails not showing", "english", {"thumbnail-settings"}, "problem"},
        {"thumbnails not loading", "english", {"thumbnail-settings"}, "problem"},
        {"no se ven miniaturas", "spanish", {"thumbnail-settings"}, "problem-es"},
        {"miniaturas no cargan", "spanish", {"thumbnail-settings"}, "problem-es"},
        {"make the menu blurry", "english", {"scene-background"}, "problem"},
        {"hacer el menu borroso", "spanish", {"scene-background"}, "problem-es"},
        {"change menu music", "english", {"menu-music"}, "problem"},
        {"cambiar musica del menu", "spanish", {"menu-music"}, "problem-es"},
        {"show on discord", "english", {"discord-rich-presence"}, "problem"},
        {"mostrar en discord", "spanish", {"discord-rich-presence"}, "problem-es"},
        {"recolor my icons", "english", {"colorful-icons"}, "problem"},
        {"recolorear mis iconos", "spanish", {"colorful-icons"}, "problem-es"},
        {"laggy scroll", "english", {"smooth-scroll"}, "problem"},
        {"scroll con lag", "spanish", {"smooth-scroll"}, "problem-es"},
        {"take a picture of my level", "english", {"capture"}, "problem"},
        {"capturar mi nivel", "spanish", {"capture"}, "problem-es"},
        // exact names still win over soft phrases
        {"cursor", "english", {"custom-cursor"}, "problem-prec"},
        {"discord", "english", {"discord-rich-presence"}, "problem-prec"},
        {"miniaturas", "spanish", {"thumbnail-settings"}, "problem-prec"},

        // ===== Round 8: full mod coverage (new entries) =====
        {"collab", "english", {"collab-editor"}, "modfull"},
        {"collab editor", "english", {"collab-editor"}, "modfull"},
        {"editor multijugador", "spanish", {"collab-editor"}, "modfull"},
        {"edit levels with friends", "english", {"collab-editor"}, "modfull"},
        {"editor history", "english", {"editor-history"}, "modfull"},
        {"ctrl h", "english", {"editor-history"}, "modfull"},
        {"historial del editor", "spanish", {"editor-history"}, "modfull"},
        {"my levels filters", "english", {"editor-filters"}, "modfull"},
        {"filtrar mis niveles", "spanish", {"editor-filters"}, "modfull"},
        {"color picker", "english", {"editor-colorpicker"}, "modfull"},
        {"ctrl g", "english", {"editor-colorpicker"}, "modfull"},
        {"free rotate", "english", {"editor-rotate"}, "modfull"},
        {"rotar objetos", "spanish", {"editor-rotate"}, "modfull"},
        {"menu physics", "english", {"menu-physics"}, "modfull"},
        {"fisica del menu", "spanish", {"menu-physics"}, "modfull"},
        {"song search", "english", {"song-search"}, "modfull"},
        {"buscar canciones", "spanish", {"song-search"}, "modfull"},
        {"mentions", "english", {"comment-mentions"}, "modfull"},
        {"menciones", "spanish", {"comment-mentions"}, "modfull"},
        {"message notifications", "english", {"message-notifications"}, "modfull"},
        {"notificaciones de mensajes", "spanish", {"message-notifications"}, "modfull"},
        {"mod previews", "english", {"mod-previews"}, "modfull"},
        {"settings panel", "english", {"settings-panel"}, "modfull"},
        {"panel de ajustes", "spanish", {"settings-panel"}, "modfull"},
        {"smooth ui", "english", {"smooth-ui"}, "modfull"},
        {"ui suave", "spanish", {"smooth-ui"}, "modfull"},
        {"icons", "english", {"colorful-icons", "icon-maker"}, "modfull"},
        {"iconos", "spanish", {"colorful-icons", "icon-maker"}, "modfull"},
        // natural for new features
        {"how do i undo in the editor history", "english", {"editor-history"}, "modfull-nat"},
        {"como edito con amigos", "spanish", {"collab-editor"}, "modfull-nat"},
        {"search songs by name", "english", {"song-search"}, "modfull-nat"},
        {"buscar canciones por nombre", "spanish", {"song-search"}, "modfull-nat"},

        // ===== Round 9: new features (icons, dual, requests, perf...) =====
        {"icon maker", "english", {"icon-maker"}, "newfeat9"},
        {"icon creator", "english", {"icon-maker"}, "newfeat9"},
        {"make icons", "english", {"icon-maker"}, "newfeat9"},
        {"creador de iconos", "spanish", {"icon-maker"}, "newfeat9"},
        {"hacer iconos", "spanish", {"icon-maker"}, "newfeat9"},
        {"icon gallery", "english", {"icon-gallery"}, "newfeat9"},
        {"icon store", "english", {"icon-gallery"}, "newfeat9"},
        {"download icons", "english", {"icon-gallery"}, "newfeat9"},
        {"tienda de iconos", "spanish", {"icon-gallery"}, "newfeat9"},
        {"icon gradients", "english", {"icon-gradients"}, "newfeat9"},
        {"gradient icons", "english", {"icon-gradients"}, "newfeat9"},
        {"degradados de iconos", "spanish", {"icon-gradients"}, "newfeat9"},
        {"separate dual", "english", {"separate-dual"}, "newfeat9"},
        {"dual icons", "english", {"separate-dual"}, "newfeat9"},
        {"iconos del jugador 2", "spanish", {"separate-dual"}, "newfeat9"},
        {"golden best", "english", {"golden-best"}, "newfeat9"},
        {"gold percentage", "english", {"golden-best"}, "newfeat9"},
        {"porcentaje dorado", "spanish", {"golden-best"}, "newfeat9"},
        {"death effects", "english", {"death-effects"}, "newfeat9"},
        {"custom death", "english", {"death-effects"}, "newfeat9"},
        {"efecto de muerte", "spanish", {"death-effects"}, "newfeat9"},
        {"performance mode", "english", {"gameplay-performance"}, "newfeat9"},
        {"fps boost", "english", {"gameplay-performance"}, "newfeat9"},
        {"modo rendimiento", "spanish", {"gameplay-performance"}, "newfeat9"},
        {"copy icons", "english", {"icon-copy"}, "newfeat9"},
        {"icon sets", "english", {"icon-copy"}, "newfeat9"},
        {"copiar iconos", "spanish", {"icon-copy"}, "newfeat9"},
        {"level requests", "english", {"level-requests"}, "newfeat9"},
        {"request queue", "english", {"level-requests"}, "newfeat9"},
        {"cola de requests", "spanish", {"level-requests"}, "newfeat9"},
        {"dynamic volume", "english", {"dynamic-volume"}, "newfeat9"},
        {"auto volume", "english", {"dynamic-volume"}, "newfeat9"},
        {"volumen dinamico", "spanish", {"dynamic-volume"}, "newfeat9"},
        {"menu loop", "english", {"menu-music", "menu-loop"}, "newfeat9"},
        {"menu loop control", "english", {"menu-loop"}, "newfeat9"},
        {"now playing", "english", {"menu-loop"}, "newfeat9"},
        {"control de loop", "spanish", {"menu-loop"}, "newfeat9"},
        {"info suite", "english", {"info-suite"}, "newfeat9"},
        {"level stats", "english", {"info-suite"}, "newfeat9"},
        {"mapa de muertes", "spanish", {"info-suite"}, "newfeat9"},

        // natural phrasing for new features
        {"how do i make my own icon", "english", {"icon-maker"}, "newfeat9-nat"},
        {"quiero un icono personalizado", "spanish", {"icon-maker"}, "newfeat9-nat"},
        {"donde esta la tienda de iconos", "spanish", {"icon-gallery"}, "newfeat9-nat"},
        {"can i have different icons for the second player", "english", {"separate-dual"}, "newfeat9-nat"},
        {"como pongo iconos distintos en el dual", "spanish", {"separate-dual"}, "newfeat9-nat"},
        {"my percentage should be gold on new best", "english", {"golden-best"}, "newfeat9-nat"},
        {"donde cambio el efecto de muerte", "spanish", {"death-effects"}, "newfeat9-nat"},
        {"make the game run faster", "english", {"gameplay-performance"}, "newfeat9-nat"},
        {"como abro la cola de pedidos", "spanish", {"level-requests"}, "newfeat9-nat"},
        {"even out song volumes", "english", {"dynamic-volume"}, "newfeat9-nat"},
        {"como veo las estadisticas del nivel", "spanish", {"info-suite"}, "newfeat9-nat"},

        // ===== Round 9: small-talk (conversational) =====
        {"what is the weather", "english", {"weather"}, "smalltalk"},
        {"weather", "english", {"weather"}, "smalltalk"},
        {"is it raining", "english", {"weather"}, "smalltalk"},
        {"que tiempo hace", "spanish", {"weather"}, "smalltalk"},
        {"hace frio", "spanish", {"weather"}, "smalltalk"},
        {"what is your favorite", "english", {"favorite-things"}, "smalltalk"},
        {"what do you like", "english", {"favorite-things"}, "smalltalk"},
        {"cual es tu color favorito", "spanish", {"favorite-things"}, "smalltalk"},
        {"are you happy", "english", {"feelings"}, "smalltalk"},
        {"do you sleep", "english", {"feelings"}, "smalltalk"},
        {"estas triste", "spanish", {"feelings"}, "smalltalk"},
        {"are you an ai", "english", {"capabilities"}, "smalltalk"},
        {"can you play", "english", {"capabilities"}, "smalltalk"},
        {"eres inteligente", "spanish", {"capabilities"}, "smalltalk"},
        {"where are you from", "english", {"origin"}, "smalltalk"},
        {"are you from teyvat", "english", {"origin"}, "smalltalk"},
        {"de donde eres", "spanish", {"origin"}, "smalltalk"},
        // small-talk shouldn't steal technical queries
        {"paimon", "english", {"mod-about", "hub"}, "smalltalk-prec"},
        {"cursor", "english", {"custom-cursor"}, "smalltalk-prec"},
        {"musica", "spanish", {"menu-music"}, "smalltalk-prec"},
    };

    int pass = 0, fail = 0;
    std::vector<std::string> failures;

    for (auto const& c : cases) {
        auto norm = normalize(c.query);
        auto toks = tokenize(norm);
        auto res = PaigoritV1::run(intents, norm, toks, c.lang);

        std::string got = res.best ? res.best->id : FALLBACK;

        bool ok = false;
        for (auto const& e : c.expect) if (e == got) { ok = true; break; }

        if (ok) {
            ++pass;
        } else {
            ++fail;
            std::string exp;
            for (auto const& e : c.expect) { exp += e; exp += " "; }
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "[%-12s] '%s' (%s) -> got '%s' fuzzy=%.0f, expected: %s",
                c.group.c_str(), c.query.c_str(), c.lang.c_str(),
                got.c_str(), res.bestRawFuzzy, exp.c_str());
            failures.push_back(buf);
        }
    }

    std::printf("\n=== Paigorit harness ===\n");
    for (auto const& f : failures) std::printf("FAIL %s\n", f.c_str());
    int total = pass + fail;
    std::printf("\n%d/%d passed (%.1f%%), %d failed\n",
                pass, total, total ? 100.0 * pass / total : 0.0, fail);

    // ---- Multi-topic detection (splitTopics) ----
    struct MultiCase {
        std::string query;
        std::string lang;
        std::vector<std::string> expectIds; // empty => expect no multi-topic
    };
    std::vector<MultiCase> multiCases = {
        {"cursor and discord", "english", {"custom-cursor", "discord-rich-presence"}},
        {"menu music and background", "english", {"menu-music", "scene-background"}},
        {"cursor and discord and pet", "english", {"custom-cursor", "discord-rich-presence", "pet"}},
        {"cursor y discord", "spanish", {"custom-cursor", "discord-rich-presence"}},
        {"musica y fondos", "spanish", {"menu-music", "scene-background"}},
        // not multi-topic: no conjunction
        {"profile background", "english", {}},
        {"discord", "english", {}},
        // conjunction but only one real topic
        {"cursor and stuff", "english", {}},
        {"hello and bye", "english", {}},
    };

    int mpass = 0, mfail = 0;
    for (auto const& c : multiCases) {
        auto norm = normalize(c.query);
        auto topics = PaigoritV1::splitTopics(intents, norm, c.lang);
        std::vector<std::string> gotIds;
        for (auto const* t : topics) gotIds.push_back(t->id);

        bool ok = (gotIds == c.expectIds);
        if (ok) { ++mpass; }
        else {
            ++mfail;
            std::string got, exp;
            for (auto const& g : gotIds) { got += g; got += " "; }
            for (auto const& e : c.expectIds) { exp += e; exp += " "; }
            std::printf("FAIL [multi] '%s' (%s) -> got [%s] expected [%s]\n",
                        c.query.c_str(), c.lang.c_str(), got.c_str(), exp.c_str());
        }
    }
    std::printf("multi-topic: %d/%d passed\n", mpass, mpass + mfail);

    return (fail == 0 && mfail == 0) ? 0 : 1;
}
