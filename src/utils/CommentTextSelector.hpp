#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCScrollLayerExt.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace paimon {

class CommentTextSelector : public cocos2d::CCLayer {
protected:
    struct DisplaySegment {
        size_t rawStart = 0;
        size_t rawEnd = 0;
        cocos2d::CCRect bounds;
    };
    struct DisplayLine {
        cocos2d::CCRect bounds;
        std::vector<DisplaySegment> segments;
    };

    std::string m_fullText;
    geode::WeakRef<cocos2d::CCNode> m_textNode;
    bool m_selecting = false;
    bool m_gestureCancelled = false;
    geode::Ref<cocos2d::CCTouch> m_touch;
    cocos2d::CCPoint m_startPos;
    cocos2d::CCPoint m_endPos;
    size_t m_startIndex = 0;
    size_t m_endIndex = 0;
    geode::WeakRef<CCScrollLayerExt> m_parentScroll;
    bool m_parentScrollWasDisabled = false;
    std::chrono::steady_clock::time_point m_lastTap{};
    cocos2d::CCPoint m_lastTapPosition;
    int m_tapCount = 0;

    cocos2d::CCDrawNode* m_highlight = nullptr;
    cocos2d::CCMenu* m_copyMenu = nullptr;
    cocos2d::CCLabelBMFont* m_copyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_allLabel = nullptr;
    cocos2d::CCRect m_textRect;
    std::vector<DisplayLine> m_lines;

    bool init(std::string const& text, cocos2d::CCNode* textNode,
              cocos2d::CCSize const& cellSize);
    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void refresh(std::string const& text, cocos2d::CCNode* textNode,
                 cocos2d::CCSize const& cellSize);
    void lockParentScroll();
    void unlockParentScroll();
    void onLongPress(float);
    void cancelMentionTouches();
    void activateMention(cocos2d::CCPoint const& point);
    bool isVisibleAt(cocos2d::CCPoint const& worldPoint);
    void rebuildLayoutCache();
    cocos2d::CCRect getExpandedTextRect() const;
    size_t pointToTextIndex(cocos2d::CCPoint const& point) const;
    void updateSelection(cocos2d::CCPoint const& point);
    void selectWord(cocos2d::CCPoint const& point);
    void updateHighlight();
    void showCopyMenu();
    std::string getSelectedText() const;
    void onCopy(cocos2d::CCObject*);
    void onSelectAll(cocos2d::CCObject*);
    void dismissSelection();

public:
    static CommentTextSelector* create(std::string const& text,
                                       cocos2d::CCNode* textNode,
                                       cocos2d::CCSize const& cellSize);
    static void attach(cocos2d::CCNode* parent, std::string const& text,
                       cocos2d::CCNode* textNode);
    static bool handleKeyboard(geode::KeyboardInputData& data);
};

} // namespace paimon
