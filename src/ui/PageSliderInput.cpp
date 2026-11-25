#include "PageSliderInput.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace geode::prelude;

namespace paimon::ui {

PageSliderInput* PageSliderInput::create(float width, std::function<void(float)> onChanged) {
    auto* input = new PageSliderInput();
    if (input->initInput(width, std::move(onChanged))) {
        input->autorelease();
        return input;
    }
    delete input;
    return nullptr;
}

bool PageSliderInput::initInput(float width, std::function<void(float)> onChanged) {
    if (!CCLayer::init() || width <= 0.f) return false;
    setContentSize({width, 24.f});
    setAnchorPoint({0.f, 0.f});
    m_onChanged = std::move(onChanged);
    setTouchEnabled(true);
    return true;
}

void PageSliderInput::setThumbState(float width, float progress) {
    m_thumbWidth = std::clamp(width, 0.f, getContentWidth());
    if (!isDragging()) m_progress = std::clamp(progress, 0.f, 1.f);
}

void PageSliderInput::cancelDrag() {
    m_touchID = -1;
    m_dragOffset = 0.f;
}

void PageSliderInput::onExit() {
    cancelDrag();
    CCLayer::onExit();
}

void PageSliderInput::registerWithTouchDispatcher() {
    // BoomScrollLayer registers at -138. Claim only our track before it sees
    // the gesture, so it cannot start a simultaneous swipe of the level card.
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -139, true);
}

bool PageSliderInput::canInteract() {
    if (!getParent() || !isTouchEnabled()) return false;
    for (auto* node = static_cast<CCNode*>(this); node; node = node->getParent()) {
        if (!node->isVisible()) return false;
    }
    return true;
}

void PageSliderInput::scrubTo(float x) {
    float const travel = getContentWidth() - m_thumbWidth;
    m_progress = travel > 0.f
        ? std::clamp((x - m_dragOffset - m_thumbWidth * 0.5f) / travel, 0.f, 1.f)
        : 0.f;
    if (m_onChanged) m_onChanged(m_progress);
}

bool PageSliderInput::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (isDragging() || !canInteract()) return false;
    auto const point = convertToNodeSpace(touch->getLocation());
    if (!CCRect{0.f, 0.f, getContentWidth(), getContentHeight()}.containsPoint(point)) return false;

    float const center = m_thumbWidth * 0.5f + m_progress * (getContentWidth() - m_thumbWidth);
    m_dragOffset = std::abs(point.x - center) <= m_thumbWidth * 0.5f ? point.x - center : 0.f;
    m_touchID = touch->getID();
    scrubTo(point.x);
    return true;
}

void PageSliderInput::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!isDragging() || touch->getID() != m_touchID) return;
    if (!canInteract()) {
        cancelDrag();
        return;
    }
    scrubTo(convertToNodeSpace(touch->getLocation()).x);
}

void PageSliderInput::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (!isDragging() || touch->getID() != m_touchID) return;
    ccTouchMoved(touch, event);
    cancelDrag();
}

void PageSliderInput::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    if (isDragging() && touch->getID() == m_touchID) cancelDrag();
}

} // namespace paimon::ui
