#pragma once

#include "CollabTypes.hpp"
#include "../emotes/EmoteRenderer.hpp"

#include <Geode/Geode.hpp>
#include <string>

// Chat line rendering for collab, reusing the mod's existing emote system
// (EmoteRenderer + the downloaded emote catalog). A line with `:emote:` syntax
// is rendered with images; anything else is a plain colored label.
namespace paimon::collab {

inline cocos2d::CCNode* buildChatLine(ChatMessage const& msg, float textScale) {
    std::string line = msg.from == 0 ? msg.text : fmt::format("{}: {}", msg.name, msg.text);

    if (paimon::emotes::EmoteRenderer::hasEmoteSyntax(line)) {
        if (auto* node = paimon::emotes::EmoteRenderer::renderComment(
                line, 16.f, 320.f, "chatFont.fnt", textScale)) {
            node->setAnchorPoint({0.f, 0.f});
            return node;
        }
    }

    std::string text = line;
    if (text.size() > 90) text = text.substr(0, 87) + "...";
    auto* label = cocos2d::CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setAnchorPoint({0.f, 0.f});
    label->setScale(textScale);
    label->setColor(msg.from == 0 ? cocos2d::ccColor3B{200, 200, 200} : peerColor(msg.from));
    return label;
}

} // namespace paimon::collab
