#include "IconPalettes.hpp"

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

GradientSpec linearOf(float angle, std::vector<GradientStop> stops) {
    GradientSpec spec;
    spec.kind = GradientKind::Linear;
    spec.angleDeg = angle;
    spec.stops = std::move(stops);
    return spec;
}

GradientSpec radialOf(std::vector<GradientStop> stops) {
    GradientSpec spec;
    spec.kind = GradientKind::Radial;
    spec.centerX = 0.5f;
    spec.centerY = 0.5f;
    spec.radius = 0.75f;
    spec.stops = std::move(stops);
    return spec;
}

}  // anonymous namespace

std::vector<cocos2d::ccColor3B> const& quickColors() {
    static std::vector<cocos2d::ccColor3B> const colors{
        {255, 255, 255}, {190, 195, 205}, {110, 116, 130}, {32, 34, 42}, {0, 0, 0},
        {255,  70,  70}, {255, 130,  60}, {255, 200,  50}, {245, 255, 90},
        {120, 230,  90}, { 40, 200, 120}, { 60, 220, 220}, { 70, 160, 255},
        { 80,  95, 235}, {160,  90, 245}, {235,  90, 220}, {255, 105, 160},
        {180, 120,  70}, {255, 215, 165}, {140,  35,  35},
    };
    return colors;
}

std::vector<GradientPreset> const& gradientPresets() {
    static std::vector<GradientPreset> const presets{
        {"Fuego",     linearOf(90.f, {{0.00f, {255, 220,  80, 255}},
                                      {0.50f, {255, 120,  30, 255}},
                                      {1.00f, {190,  30,  30, 255}}})},
        {"Hielo",     linearOf(90.f, {{0.00f, {235, 252, 255, 255}},
                                      {0.55f, {120, 205, 255, 255}},
                                      {1.00f, { 40,  95, 190, 255}}})},
        {"Atardecer", linearOf(90.f, {{0.00f, {255, 190, 100, 255}},
                                      {0.50f, {255, 100, 140, 255}},
                                      {1.00f, { 90,  60, 190, 255}}})},
        {"Neon",      linearOf(45.f, {{0.00f, { 60, 255, 220, 255}},
                                      {1.00f, {180,  70, 255, 255}}})},
        {"Arcoiris",  linearOf(0.f,  {{0.00f, {255,  60,  60, 255}},
                                      {0.25f, {255, 220,  60, 255}},
                                      {0.50f, { 80, 235, 110, 255}},
                                      {0.75f, { 70, 170, 255, 255}},
                                      {1.00f, {200,  90, 255, 255}}})},
        {"Oro",       linearOf(90.f, {{0.00f, {255, 245, 180, 255}},
                                      {0.45f, {235, 185,  60, 255}},
                                      {1.00f, {150,  95,  20, 255}}})},
        {"Sombra",    linearOf(90.f, {{0.00f, {120, 128, 148, 255}},
                                      {1.00f, { 18,  20,  30, 255}}})},
        {"Foco",      radialOf({{0.00f, {255, 255, 255, 255}},
                                {1.00f, { 55,  70, 130, 255}}})},
    };
    return presets;
}

std::vector<cocos2d::ccColor3B> playerColors() {
    std::vector<cocos2d::ccColor3B> out;
    auto* gm = GameManager::get();
    if (!gm) return out;
    out.push_back(gm->colorForIdx(gm->getPlayerColor()));
    out.push_back(gm->colorForIdx(gm->getPlayerColor2()));
    out.push_back(gm->colorForIdx(gm->getPlayerGlowColor()));
    return out;
}

}  // namespace paimon::icon_maker
