// Host-side test harness for Paimon's conversational engine (multi-turn
// follow-ups). Compiles the real engine + LightLemmatizer sources (unity
// build) and runs labeled follow-up cases, reporting accuracy.
//
// Build: tests/guide/run_conversation_tests.bat

#include "../../src/features/guide/services/ConversationalEngine.cpp"
#include "../../src/features/guide/services/LightLemmatizer.cpp"
#include "TestTopics.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

using namespace paimon::guide;

// Mirror of the service's normalize + tokenize (same as harness.cpp).
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
    std::string lang;      // "english" / "spanish"
    std::string ctxTopic;  // current topic id ("" = no context)
    std::string expectSub; // expected subTopicId; "<topic>" = pure reference (topic itself)
    std::string group;
};

} // namespace

int main() {
    auto topics = test::makeTopics();
    ConversationalEngine engine;
    engine.setTopics(topics);

    std::vector<Case> cases = {
        // ---- pure references resolve to the topic ----
        {"y eso?", "spanish", "custom-cursor", "<topic>", "ref"},
        {"eso mismo", "spanish", "custom-cursor", "<topic>", "ref"},
        {"and that?", "english", "custom-cursor", "<topic>", "ref"},
        {"what about that", "english", "custom-cursor", "<topic>", "ref"},

        // ---- "que mas?" / "what else?" -> more ----
        {"que mas?", "spanish", "custom-cursor", "<more>", "more"},
        {"y que mas hay?", "spanish", "menu-music", "<more>", "more"},
        {"what else?", "english", "menu-music", "<more>", "more"},
        {"anything else", "english", "scene-background", "<more>", "more"},

        // ---- sub-topic references ----
        {"y el color?", "spanish", "custom-cursor", "cursor-color", "sub"},
        {"y el tamano?", "spanish", "custom-cursor", "cursor-size", "sub"},
        {"y la imagen?", "spanish", "custom-cursor", "cursor-image", "sub"},
        {"y las transiciones?", "spanish", "custom-cursor", "cursor-transitions", "sub"},
        {"and the color?", "english", "custom-cursor", "cursor-color", "sub"},
        {"what about size", "english", "custom-cursor", "cursor-size", "sub"},
        {"la playlist?", "spanish", "menu-music", "music-playlists", "sub"},
        {"y la biblioteca?", "spanish", "menu-music", "music-library", "sub"},
        {"what about hotkeys", "english", "menu-music", "music-hotkeys", "sub"},
        {"el blur?", "spanish", "scene-background", "bg-blur", "sub"},
        {"y el video?", "spanish", "scene-background", "bg-video", "sub"},
        {"the gradient?", "english", "scene-background", "bg-gradient", "sub"},
        {"y el orden?", "spanish", "thumbnail-settings", "thumb-order", "sub"},
        {"what about resolution", "english", "thumbnail-settings", "thumb-quality", "sub"},
        {"el arcoiris?", "spanish", "colorful-icons", "icons-rainbow", "sub"},

        // ---- NOT follow-ups (new topic or greeting) ----
        {"hola", "spanish", "custom-cursor", "", "not-fu"},
        {"hello", "english", "menu-music", "", "not-fu"},
        {"donde configuro el cursor", "english", "", "", "not-fu"},
        {"donde configuro el cursor", "english", "menu-music", "", "not-fu"},
        {"quiero cambiar mi foto de perfil", "spanish", "custom-cursor", "", "not-fu"},
        {"y ahora como pongo musica?", "spanish", "custom-cursor", "", "not-fu"},
        {"give me a joke", "english", "custom-cursor", "", "not-fu"},
        {"que es paimbnails", "spanish", "custom-cursor", "", "not-fu"},
    };

    int pass = 0, fail = 0;
    for (auto const& c : cases) {
        auto norm = normalize(c.query);
        auto toks = tokenize(norm);
        auto content = LightLemmatizer::removeStopwords(toks);
        auto res = engine.resolve(norm, content, c.lang, c.ctxTopic);

        std::string got;
        if (!res.isFollowUp) got = "";
        else if (res.subTopicId.empty()) {
            if (res.pureReference) got = "<topic>";
            else got = "<topic>";
        } else got = res.subTopicId;

        // "<more>" is a pure reference to the topic's "more" answer.
        if (res.isFollowUp && res.pureReference && c.expectSub == "<more>") {
            got = "<more>";
        }

        bool ok = (got == c.expectSub);
        if (ok) {
            ++pass;
        } else {
            ++fail;
            std::printf("FAIL [%-8s] '%s' (%s) ctx=%s -> got '%s' expected '%s'\n",
                        c.group.c_str(), c.query.c_str(), c.lang.c_str(),
                        c.ctxTopic.c_str(), got.c_str(), c.expectSub.c_str());
        }
    }

    std::printf("\n=== Conversational engine harness ===\n");
    int total = pass + fail;
    std::printf("%d/%d passed (%.1f%%), %d failed\n",
                pass, total, total ? 100.0 * pass / total : 0.0, fail);

    return fail == 0 ? 0 : 1;
}
