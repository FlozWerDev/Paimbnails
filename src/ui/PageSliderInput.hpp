#pragma once

#include <Geode/Geode.hpp>
#include <functional>

namespace paimon::ui {

// touch surface for a separately drawn scrollbar. Coordinates are local to
// the track, with a taller hit area so a thin bar is easy to grab.
class PageSliderInput : public cocos2d::CCLayer {
public:
    static PageSliderInput* create(float width, std::function<void(float)> onChanged);
    void setThumbState(float width, float progress);
    void cancelDrag();
    bool isDragging() const { return m_touchID >= 0; }
    float progress() const { return m_progress; }

    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

private:
    bool initInput(float width, std::function<void(float)> onChanged);
    bool canInteract();
    void scrubTo(float x);

    std::function<void(float)> m_onChanged;
    float m_thumbWidth = 0.f;
    float m_progress = 0.f;
    float m_dragOffset = 0.f;
    int m_touchID = -1;
};

} // namespace paimon::ui
