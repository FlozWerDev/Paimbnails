#include "IconThemes.hpp"

#include "IconPalettes.hpp"

#include <Geode/Geode.hpp>

namespace paimon::icon_maker {

namespace {

FillSpec flatOf(cocos2d::ccColor4B color) {
    FillSpec fill;
    fill.type = FillType::Flat;
    fill.flat = color;
    return fill;
}

FillSpec gradientNamed(std::string_view name, float angle) {
    FillSpec fill;
    fill.type = FillType::Gradient;
    for (auto const& preset : gradientPresets()) {
        if (preset.name != name) continue;
        fill.gradient = preset.spec;
        fill.gradient.angleDeg = angle;
        break;
    }
    return fill;
}

FillSpec patternOf(PatternKind kind, cocos2d::ccColor4B a, cocos2d::ccColor4B b,
                   float scale = 1.f, float angle = 45.f) {
    FillSpec fill;
    fill.type = FillType::Pattern;
    fill.pattern.kind = kind;
    fill.pattern.primary = a;
    fill.pattern.secondary = b;
    fill.pattern.scale = scale;
    fill.pattern.angleDeg = angle;
    return fill;
}

FillSpec chromaFill() {
    FillSpec fill;
    fill.type = FillType::Flat;
    fill.flat = {255, 255, 255, 255};
    fill.chroma = true;
    return fill;
}

IconTheme themeOf(std::string name, FillSpec main, FillSpec secondary, FillSpec glow) {
    IconTheme theme;
    theme.name = std::move(name);
    theme.main = std::move(main);
    theme.secondary = std::move(secondary);
    theme.tertiary = theme.secondary;
    theme.glow = std::move(glow);
    return theme;
}

}  // namespace

std::vector<IconTheme> const& iconThemes() {
    static std::vector<IconTheme> const themes{
        themeOf("Fuego",     gradientNamed("Fuego", 90.f),
                             flatOf({255, 205,  70, 255}), flatOf({255, 120,  40, 255})),
        themeOf("Hielo",     gradientNamed("Hielo", 90.f),
                             flatOf({225, 248, 255, 255}), flatOf({110, 205, 255, 255})),
        themeOf("Cyber",     gradientNamed("Cyber", 135.f),
                             flatOf({ 60, 255, 235, 255}), flatOf({255,  60, 190, 255})),
        themeOf("Galaxia",   gradientNamed("Galaxia", 0.f),
                             flatOf({235, 160, 255, 255}), flatOf({130, 100, 255, 255})),
        themeOf("Esmeralda", gradientNamed("Esmeralda", 90.f),
                             flatOf({200, 255, 210, 255}), flatOf({ 50, 255, 165, 255})),
        themeOf("Sakura",    gradientNamed("Sakura", 90.f),
                             flatOf({255, 240, 248, 255}), flatOf({255, 130, 195, 255})),
        themeOf("Atardecer", gradientNamed("Atardecer", 90.f),
                             flatOf({255, 175, 120, 255}), flatOf({255, 110, 150, 255})),
        themeOf("Neon",      gradientNamed("Neon", 45.f),
                             flatOf({ 60, 255, 220, 255}), flatOf({180,  70, 255, 255})),
        themeOf("Oro",       gradientNamed("Oro", 90.f),
                             flatOf({120,  75,  20, 255}), flatOf({255, 225, 130, 255})),
        themeOf("Carbono",   patternOf(PatternKind::Carbon, {54, 58, 70, 255}, {22, 24, 32, 255}, 1.2f, 0.f),
                             flatOf({255,  85,  60, 255}), flatOf({255,  90,  60, 255})),
        themeOf("Prisma",    gradientNamed("Prisma", 0.f),
                             flatOf({255, 255, 255, 255}), flatOf({120, 240, 255, 255})),
        themeOf("Sombra",    gradientNamed("Sombra", 90.f),
                             flatOf({ 26,  28,  38, 255}), flatOf({150, 100, 255, 255})),
        themeOf("Veneno",    flatOf({110, 225,  90, 255}),
                             flatOf({ 30, 110,  55, 255}), flatOf({170, 255, 110, 255})),
        themeOf("Oceano",    flatOf({ 55, 140, 235, 255}),
                             flatOf({ 20,  50, 120, 255}), flatOf({ 70, 225, 235, 255})),
        themeOf("Arcoiris",  chromaFill(), chromaFill(), flatOf({255, 255, 255, 255})),
    };
    return themes;
}

bool currentKitTheme(IconTheme& out) {
    auto colors = playerColors();
    if (colors.size() < 3) return false;

    out = themeOf("Mis colores",
        flatOf({colors[0].r, colors[0].g, colors[0].b, 255}),
        flatOf({colors[1].r, colors[1].g, colors[1].b, 255}),
        flatOf({colors[2].r, colors[2].g, colors[2].b, 255}));
    return true;
}

bool themeFillFor(IconTheme const& theme, std::string_view slotKey, FillSpec& out) {
    if (slotKey == "main")      { out = theme.main;      return true; }
    if (slotKey == "secondary") { out = theme.secondary; return true; }
    if (slotKey == "tertiary")  { out = theme.tertiary;  return true; }
    if (slotKey == "glow")      { out = theme.glow;      return true; }
    if (slotKey == "extra" && theme.paintExtra) { out = theme.extra; return true; }
    return false;
}

}  // namespace paimon::icon_maker
