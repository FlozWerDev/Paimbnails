#pragma once

#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <vector>
#include <variant>

namespace paimon::emotes {

struct TextToken {
    std::string text;
    size_t start = 0;
    size_t end = 0;
};

struct EmoteToken {
    std::string name;
    size_t start = 0;
    size_t end = 0;
};

struct MentionToken {
    std::string username;
    size_t start = 0;
    size_t end = 0;
};

using CommentToken = std::variant<TextToken, EmoteToken, MentionToken>;

class EmoteRenderer {
public:
    static std::vector<CommentToken> parseTokens(std::string const& text);

    static bool hasEmoteSyntax(std::string const& text);

    static bool hasMentionSyntax(std::string const& text);

    static cocos2d::CCNode* renderComment(
        std::string const& text,
        float emoteSize = 0.f,
        float maxWidth = 200.f,
        const char* font = "chatFont.fnt",
        float fontSize = 0.45f,
        bool forceRender = false,
        // false in long comment lists (infolayer afk): first frame only.
        bool animateGifs = true
    );
};

} // namespace paimon::emotes
