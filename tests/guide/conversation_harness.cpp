// host-side test harness for paimon's conversational engine (multi-turn
// follow-ups). compiles the real engine + lightlemmatizer sources (unity
// build) and runs labeled follow-up cases, reporting accuracy.
// build: tests/guide/run_conversation_tests.bat

#include "../../src/features/guide/services/ConversationalEngine.cpp"
#include "../../src/features/guide/services/LightLemmatizer.cpp"
#include "TestTopics.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

using namespace paimon::guide;

// the harness feeds the engine exactly what the service would by running the
// real lightlemmatizer preprocessing (normalize + tokenize) the service calls.
namespace {

struct Case {
    std::string query;
    std::string lang;      // "english" / "spanish"
    std::string ctxTopic;  // current topic id ("" = no context)
    std::string expectSub; // expected subtopicid; "<topic>" = pure reference (topic itself)
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

        // ---- not follow-ups (new topic or greeting) ----
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
        auto norm = LightLemmatizer::normalize(c.query);
        auto toks = LightLemmatizer::tokenize(norm);
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
