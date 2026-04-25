#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <variant>

namespace paimon::emotes {

// Token types for parsed comment text
struct TextToken {
    std::string text;
};

struct EmoteToken {
    std::string name; // the emote identifier (without : or < >)
};

using CommentToken = std::variant<TextToken, EmoteToken>;

class EmoteRenderer {
public:
    // Parse a comment string into tokens.
    // Recognizes both :emotename: and <emotename> syntax.
    // Only matches names that exist in the loaded emote catalog.
    static std::vector<CommentToken> parseTokens(std::string const& text);

    // Returns true if the text contains any valid emote syntax worth parsing
    static bool hasEmoteSyntax(std::string const& text);

    // Render a comment with emotes as a mixed CCNode (text labels + emote sprites).
    // The returned node should replace the original comment label.
    // emoteSize: pixel size of each emote sprite. 0 = auto-detect from font line height.
    // maxWidth: horizontal wrap boundary.
    // font: BMFont file for text segments.
    // forceRender: if true, always return a rendered node even when no emotes are present
    //              (useful for custom font rendering).
    static cocos2d::CCNode* renderComment(
        std::string const& text,
        float emoteSize = 0.f,
        float maxWidth = 200.f,
        const char* font = "chatFont.fnt",
        float fontSize = 0.45f,
        bool forceRender = false
    );
};

} // namespace paimon::emotes
