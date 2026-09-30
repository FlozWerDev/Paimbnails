#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <string>

namespace paimon::progression {

// game-style bar: round caps at any width, per-frame work is one size change.
class GDProgressBar : public cocos2d::CCNode {
public:
    static GDProgressBar* create(float width, float height);

    // bare capsule with insets set, for plain (unfilled) bars.
    static cocos2d::extension::CCScale9Sprite* makeCapsule();

    void setFillColor(cocos2d::ccColor3B color);
    void setProgress(float progress);
    void animateTo(float progress, float delay, float duration);

    // centred caption, created on first use.
    void setText(std::string const& text, float scale, cocos2d::ccColor3B color);

    void update(float dt) override;

protected:
    bool init(float width, float height);
    void applyProgress(float progress);

    float m_width = 100.f;
    float m_artHeight = 20.f;
    float m_squash = 1.f;
    float m_from = 0.f;
    float m_target = 0.f;
    float m_delay = 0.f;
    float m_elapsed = 0.f;
    float m_duration = 0.f;
    cocos2d::extension::CCScale9Sprite* m_fill = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
};

} // namespace paimon::progression
