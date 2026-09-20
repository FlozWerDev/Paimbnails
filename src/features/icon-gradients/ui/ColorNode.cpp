#include "ColorNode.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

ColorNode* ColorNode::create(bool invis, int opacity) {
    auto ret = new ColorNode();

    ret->m_opacity = opacity;

    if (!ret->init(invis)) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

namespace {

// Endless slow spin for the selection ring.
CCRepeatForever* spinForever() {
    return CCRepeatForever::create(CCRotateBy::create(8, 360));
}

// One channel dimmed for the hover state.
int dimChannel(int channel) {
    return std::clamp(channel - 55, 0, 255);
}

} // namespace

bool ColorNode::init(bool invis) {
    // Base transform shared by every point.
    setAnchorPoint({0.5f, 0.5f});
    setScale(0.8f);

    m_dot = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
    m_dot->setOpacity(invis ? 0 : m_opacity);

    m_circle = CCSprite::createWithSpriteFrameName("d_circle_01_001.png");
    m_circle->setOpacity(0);

    setContentSize(m_dot->getContentSize());

    // Every child shares the dot's center.
    CCPoint middle = getContentSize() / 2.f;
    m_dot->setPosition(middle);
    m_circle->setPosition(middle);

    m_select = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
    m_select->setScale(1.15f);
    m_select->setPosition(middle);
    m_select->setOpacity(m_opacity);
    m_select->setVisible(false);

    addChild(m_dot, 0);
    addChild(m_select, 1);
    addChild(m_circle, 2);

    m_imageLabel = CCLabelBMFont::create("I", "bigFont.fnt");
    m_imageLabel->setScale(0.35f);
    m_imageLabel->setPosition(getContentSize() / 2.f);
    m_imageLabel->setVisible(false);
    m_dot->addChild(m_imageLabel);
    m_dot->setCascadeOpacityEnabled(true);

    return true;
}

void ColorNode::setSelected(bool selected) {
    // bool converts to 0/1, which is exactly the below/above split.
    setZOrder(selected);

    m_isSelected = selected;

    m_select->stopAllActions();
    m_select->setVisible(selected);
    // A hidden node only ghosts the ring, so the selection stays findable.
    m_select->setOpacity(m_isHidden ? 20 : m_opacity);
    m_select->runAction(spinForever());
}

void ColorNode::setHovered(bool hovered) {
    m_isHovered = hovered;

    ccColor3B shown = m_color;
    if (hovered)
        shown = ccc3(dimChannel(m_color.r), dimChannel(m_color.g), dimChannel(m_color.b));

    m_dot->setColor(shown);
}

void ColorNode::setHidden(bool hidden, float time, bool useAction) {
    int dotTarget = hidden ? 0 : m_opacity;
    int ringTarget = (!hidden && m_isSelected) ? m_opacity : 0;

    m_isHidden = hidden;

    if (time <= 0.f && !useAction) {
        m_dot->setOpacity(dotTarget);
        m_select->setOpacity(ringTarget);
        return;
    }

    m_dot->runAction(CCFadeTo::create(time, dotTarget));
    m_select->runAction(CCFadeTo::create(time, ringTarget));

    m_isAnimating = true;
    settleAfter(time);
}

void ColorNode::setColor(const ccColor3B& color, float time) {
    m_color = color;

    if (time <= 0.f) {
        m_dot->setColor(color);
    } else {
        auto tint = CCTintTo::create(time, color.r, color.g, color.b);
        m_dot->runAction(CCEaseSineOut::create(tint));
    }

    setHovered(m_isHovered);
}

void ColorNode::setImagePath(std::string const& path) {
    m_imagePath = path;
    m_imageLabel->setVisible(!path.empty());
}

void ColorNode::setOpacity(int opacity) {
    m_opacity = opacity;

    bool hidden = m_isHidden;
    m_dot->setOpacity(hidden ? 0 : opacity);
    m_select->setOpacity((!hidden && m_isSelected) ? opacity : 0);
}

ccColor3B ColorNode::getColor() { return m_color; }
CCSprite* ColorNode::getSprite() { return m_dot; }
bool ColorNode::isHidden() { return m_isHidden; }
bool ColorNode::isAnimating() { return m_isAnimating; }
bool ColorNode::isSelected() { return m_isSelected; }

void ColorNode::flash(float time) {
    m_isAnimating = true;

    m_circle->setOpacity(std::clamp(m_opacity - 15, 0, 255));

    auto fade = CCFadeTo::create(time, 0);
    m_circle->runAction(fade);

    settleAfter(time);
}

// Delayed reconcile after a fade finishes.
void ColorNode::settleAfter(float time) {
    auto wait = CCDelayTime::create(time);
    auto done = CCCallFunc::create(this, callfunc_selector(ColorNode::onAnimationEnded));
    runAction(CCSequence::create(wait, done, nullptr));
}

void ColorNode::onAnimationEnded() { m_isAnimating = false; }
