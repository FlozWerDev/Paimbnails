#include "CommunityHubIcon.hpp"

#include "../../../utils/PaimonDrawNode.hpp"
#include <vector>

using namespace geode::prelude;

namespace paimon::community {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCanvas = 34.f;

void fillEllipse(PaimonDrawNode* d, float cx, float cy, float rx, float ry, ccColor4F const& c) {
    constexpr int kSegs = 40;
    std::vector<CCPoint> pts;
    pts.reserve(kSegs);
    for (int i = 0; i < kSegs; ++i) {
        float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(kSegs);
        pts.emplace_back(cx + rx * std::cos(a), cy + ry * std::sin(a));
    }
    d->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), c, 0.f, ccc4f(0, 0, 0, 0));
}

// flat-bottomed dome used as the shoulders of a person.
void fillDome(PaimonDrawNode* d, float cx, float baseY, float rx, float ry, ccColor4F const& c) {
    constexpr int kSegs = 28;
    std::vector<CCPoint> pts;
    pts.reserve(kSegs + 1);
    for (int i = 0; i <= kSegs; ++i) {
        float a = kPi * static_cast<float>(i) / static_cast<float>(kSegs);
        pts.emplace_back(cx + rx * std::cos(a), baseY + ry * std::sin(a));
    }
    d->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), c, 0.f, ccc4f(0, 0, 0, 0));
}

// outline layer first, then the fill, so each person gets a gd-style dark border.
void drawPerson(PaimonDrawNode* d, float cx, float baseY, float bodyRx, float bodyRy,
                float headR, float border, ccColor4F const& fill, ccColor4F const& outline) {
    float headY = baseY + bodyRy + headR * 0.55f;
    fillDome(d, cx, baseY - border, bodyRx + border, bodyRy + border, outline);
    fillEllipse(d, cx, headY, headR + border, headR + border, outline);
    fillDome(d, cx, baseY, bodyRx, bodyRy, fill);
    fillEllipse(d, cx, headY, headR, headR, fill);
}

} // namespace

CircleButtonSprite* createCommunityHubButtonSprite() {
    constexpr float S = kCanvas;
    auto* glyph = CCNode::create();
    glyph->setContentSize({S, S});
    glyph->setAnchorPoint({0.5f, 0.5f});

    auto* d = PaimonDrawNode::create();
    if (!d) {
        auto* label = CCLabelBMFont::create("C", "bigFont.fnt");
        label->setPosition({S / 2.f, S / 2.f});
        glyph->addChild(label);
    } else {
        constexpr float c = S / 2.f;
        auto const outline = ccc4f(0.36f, 0.16f, 0.02f, 1.f);
        auto const back = ccc4f(0.98f, 0.58f, 0.08f, 1.f);
        auto const front = ccc4f(1.f, 0.78f, 0.12f, 1.f);

        drawPerson(d, c - 9.5f, 8.5f, 5.5f, 5.5f, 3.4f, 1.3f, back, outline);
        drawPerson(d, c + 9.5f, 8.5f, 5.5f, 5.5f, 3.4f, 1.3f, back, outline);
        drawPerson(d, c, 8.5f, 9.f, 8.f, 5.f, 1.5f, front, outline);
        glyph->addChild(d);
    }

    auto* sprite = CircleButtonSprite::create(glyph, CircleBaseColor::Green, CircleBaseSize::Medium);
    if (sprite) sprite->setTopRelativeScale(1.f);
    return sprite;
}

} // namespace paimon::community
