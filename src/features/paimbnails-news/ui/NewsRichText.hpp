#pragma once

#include "../services/PaimbnailsNewsService.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace paimon::news {

// discord's link blue.
inline constexpr cocos2d::ccColor3B kLinkColor{0, 168, 252};

struct RichTextStyle {
    float scale = 0.5f;
    cocos2d::ccColor3B color{255, 255, 255};
    cocos2d::ccColor3B linkColor = kLinkColor;
};

using LinkCallback = std::function<void(std::string const& url, cocos2d::CCNode* sender)>;

// word-wrapped chatFont text. Spans with a url and bare http(s) URLs in the
// text become buttons that call `onLink`; words wider than `width` (long
// uRLs) are split across lines. Returns a node anchored bottom-left with
// content size {width, height}, or nullptr when there is nothing to draw.
cocos2d::CCNode* createRichText(std::vector<TextSpan> const& spans, float width,
                                RichTextStyle const& style, LinkCallback const& onLink);

} // namespace paimon::news
