#pragma once

// Shared types for the Icon Gradients feature: gradient definitions, color
// slots and a batch node that skips drawing children.

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::icon_gradients {

constexpr int MOD_DISABLED = 1;
constexpr int P2_DISABLED = 2;
constexpr int P2_FLIP = 3;
constexpr int MENU_GRADIENTS = 4;
constexpr int P2_SEPARATE = 5;

enum SpriteType {
    Icon = 1,
    Vehicle = 2,
    Animation = 3
};

enum ColorType {
    Main = 1,
    Secondary = 2,
    Glow = 3,
    White = 4,
    Line = 5,
};

struct SimplePoint {
    CCPoint pos;
    ccColor3B color;

    bool operator==(const SimplePoint& other) const {
        return pos.equals(other.pos) && color.r == other.color.r &&
               color.g == other.color.g && color.b == other.color.b;
    }
};

struct GradientConfig {

    std::vector<SimplePoint> points;
    bool isLinear = true;

    bool operator==(const GradientConfig& other) const {
        return isLinear == other.isLinear && points == other.points;
    }

    bool isEmpty(ColorType, bool);

};

struct Gradient {

    GradientConfig main;
    GradientConfig secondary;
    GradientConfig glow;
    GradientConfig white;
    GradientConfig line;

};

// Batch nodes cache vertices so child shaders never run; this subclass skips
// the cached draw entirely so gradient sprites inside batch nodes render.
class FakeSpriteBatchNode : public CCSpriteBatchNode {

public:

    void draw() override {
        CCNode::draw();
    }

    void visit() override {
        CCNode::visit();
    }

};

} // namespace paimon::icon_gradients

namespace std {
    template <>
    struct hash<paimon::icon_gradients::SimplePoint> {
        size_t operator()(const paimon::icon_gradients::SimplePoint& point) const {
            return hash<float>()(point.pos.x) ^ hash<float>()(point.pos.y) ^
                   hash<int>()(point.color.r) ^ hash<int>()(point.color.g) ^
                   hash<int>()(point.color.b);
        }
    };
}
