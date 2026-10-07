#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::text {

inline bool isGDColorTag(std::string_view inner) {
    return (inner.size() == 2 && inner[0] == 'c') || inner == "/c";
}

inline std::string stripColorCodes(std::string const& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t index = 0; index < text.size();) {
        if (text[index] == '<' && index + 3 < text.size() && text[index + 3] == '>' &&
            isGDColorTag(std::string_view(text.data() + index + 1, 2))) {
            index += 4;
        } else {
            result += text[index++];
        }
    }
    return result;
}

inline size_t nextCharacter(std::string const& text, size_t index) {
    if (index >= text.size()) return text.size();
    auto first = static_cast<unsigned char>(text[index]);
    size_t length = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    if (first < 0xc2 || first > 0xf4 || index + length > text.size()) length = 1;
    for (size_t offset = 1; offset < length; ++offset) {
        if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) != 0x80) return index + 1;
    }
    return index + length;
}

inline size_t prevCharacter(std::string const& text, size_t index) {
    if (index == 0 || index > text.size()) return 0;
    size_t start = index - 1;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
    return start;
}

class CommentTextRange : public cocos2d::CCObject {
public:
    size_t start = 0;
    size_t end = 0;
    bool atomic = false;

    static void attach(cocos2d::CCNode* node, size_t start, size_t end, bool atomic = false) {
        auto* range = new CommentTextRange();
        range->start = start;
        range->end = end;
        range->atomic = atomic;
        range->autorelease();
        node->setUserObject("paimon-comment-text-range", range);
    }
};

struct GlyphRange {
    size_t start = 0;
    size_t end = 0;
    cocos2d::CCRect bounds;
};

inline cocos2d::CCRect unionRect(cocos2d::CCRect const& first, cocos2d::CCRect const& second) {
    float left = std::min(first.getMinX(), second.getMinX());
    float bottom = std::min(first.getMinY(), second.getMinY());
    return {left, bottom, std::max(first.getMaxX(), second.getMaxX()) - left,
            std::max(first.getMaxY(), second.getMaxY()) - bottom};
}

inline cocos2d::CCRect convertRect(cocos2d::CCNode* source, cocos2d::CCNode* target,
                                  cocos2d::CCRect const& rect) {
    auto first = target->convertToNodeSpace(source->convertToWorldSpace(rect.origin));
    cocos2d::CCRect result(first.x, first.y, 0.f, 0.f);
    for (auto point : {cocos2d::CCPoint{rect.getMaxX(), rect.getMinY()},
                       cocos2d::CCPoint{rect.getMinX(), rect.getMaxY()},
                       cocos2d::CCPoint{rect.getMaxX(), rect.getMaxY()}}) {
        auto converted = target->convertToNodeSpace(source->convertToWorldSpace(point));
        float left = std::min(result.getMinX(), converted.x);
        float bottom = std::min(result.getMinY(), converted.y);
        result = {left, bottom, std::max(result.getMaxX(), converted.x) - left,
                  std::max(result.getMaxY(), converted.y) - bottom};
    }
    return result;
}

inline std::vector<GlyphRange> labelGlyphs(cocos2d::CCLabelBMFont* label,
                                          std::string const& text, size_t start, size_t end) {
    using namespace cocos2d;
    using geode::cast::typeinfo_cast;
    std::vector<GlyphRange> ranges;
    if (!label->m_sString) return ranges;

    int lineCount = 1;
    size_t glyphCount = 0;
    for (auto* ch = label->m_sString; *ch; ++ch) {
        if (*ch == '\n') ++lineCount;
        ++glyphCount;
    }
    float lineHeight = label->getContentSize().height / lineCount;
    float pixelScale = std::max(CCDirector::get()->getContentScaleFactor(), 0.01f);
    std::vector<CCNode*> glyphs(glyphCount, nullptr);
    for (auto* child : geode::cocos::CCArrayExt<CCNode*>(label->getChildren())) {
        int const tag = child->getTag();
        if (tag >= 0 && static_cast<size_t>(tag) < glyphCount && !glyphs[tag]) {
            glyphs[tag] = child;
        }
    }
    float cursorX = 0.f;
    int line = 0;
    size_t raw = start;
    for (int slot = 0; label->m_sString[slot] && raw < end; ++slot) {
        unsigned int code = label->m_sString[slot];
        if (code == '\n') {
            if (text[raw] == '\n') ++raw;
            ++line;
            cursorX = 0.f;
            continue;
        }

        auto next = std::min(nextCharacter(text, raw), end);
        auto* sprite = typeinfo_cast<CCSprite*>(glyphs[slot]);
        tCCFontDefHashElement* definition = nullptr;
        if (auto* config = label->getConfiguration()) {
            HASH_FIND_INT(config->m_pFontDefDictionary, &code, definition);
        }
        float width = definition ? definition->fontDef.xAdvance / pixelScale : 0.f;
        if (sprite) {
            auto glyph = sprite->boundingBox();
            cursorX = glyph.getMinX() - (definition ? definition->fontDef.xOffset / pixelScale : 0.f);
            width = std::max(width, glyph.size.width);
        }
        float bottom = label->getContentSize().height - (line + 1) * lineHeight;
        ranges.push_back({raw, next, {cursorX, bottom, std::max(width, 1.f), lineHeight}});
        cursorX += width;
        raw = next;
        if (code >= 0xd800 && code <= 0xdbff && label->m_sString[slot + 1] >= 0xdc00 &&
            label->m_sString[slot + 1] <= 0xdfff) ++slot;
    }
    return ranges;
}

} // namespace paimon::text
