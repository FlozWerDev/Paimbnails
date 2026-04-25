#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon {

/// Overlay layer that enables click-and-drag text selection on a comment cell.
/// Attach via CommentTextSelector::attach() after loadFromComment finishes.
/// Selected text is highlighted and can be copied via a "Copy" button.
class CommentTextSelector : public cocos2d::CCLayer {
protected:
    struct DisplaySegment {
        size_t rawStart = 0;
        size_t rawEnd = 0;
        float startX = 0.f;
        float endX = 0.f;
    };

    struct DisplayLine {
        size_t rawStart = 0;
        size_t rawEnd = 0;
        float width = 0.f;
        std::vector<DisplaySegment> segments;
    };

    std::string m_fullText;
    std::string m_fontFile = "chatFont.fnt";
    cocos2d::CCNode* m_textNode = nullptr;       // the emote overlay or TextArea/label

    // Selection state
    bool m_selecting = false;
    cocos2d::CCPoint m_startPos{0.f, 0.f};
    cocos2d::CCPoint m_endPos{0.f, 0.f};
    size_t m_startIndex = 0;
    size_t m_endIndex = 0;

    // Visual
    cocos2d::CCDrawNode* m_highlight = nullptr;
    cocos2d::CCMenu* m_copyMenu = nullptr;

    cocos2d::CCRect m_textRect;
    float m_availableWidth = 0.f;
    float m_lineHeight = 0.f;
    float m_effectiveFontScale = 1.f;
    std::vector<DisplayLine> m_lines;

    bool init(std::string const& text, cocos2d::CCNode* textNode,
              cocos2d::CCSize const& cellSize, std::string const& fontFile);

    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void refresh(std::string const& text, cocos2d::CCNode* textNode,
                 cocos2d::CCSize const& cellSize, std::string const& fontFile);
    void rebuildLayoutCache();
    cocos2d::CCRect getExpandedTextRect() const;
    cocos2d::CCPoint clampToTextRect(cocos2d::CCPoint const& point) const;
    size_t pointToTextIndex(cocos2d::CCPoint const& point) const;
    void updateSelection(cocos2d::CCPoint const& point, bool resetStart = false);
    void updateHighlight();
    std::string getSelectedText() const;
    void onCopy(cocos2d::CCObject*);
    void dismissCopyButton();

public:
    static CommentTextSelector* create(std::string const& text,
                                        cocos2d::CCNode* textNode,
                                        cocos2d::CCSize const& cellSize,
                                        std::string const& fontFile = "chatFont.fnt");

    /// Convenience: attach a selector to a CommentCell's m_mainLayer.
    /// text: the raw comment string, textNode: the rendered text/emote node.
    static void attach(cocos2d::CCNode* parent, std::string const& text,
                       cocos2d::CCNode* textNode,
                       std::string const& fontFile = "chatFont.fnt");
};

} // namespace paimon
