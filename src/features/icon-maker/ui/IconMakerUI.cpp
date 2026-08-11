#include "IconMakerUI.hpp"

#include "IconMakerKit.hpp"
#include "../engine/GradientRasterizer.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_maker::ui {

namespace {

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

CCMenu* rowMenu(CCNode* row) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);
    return menu;
}

// Row on a GD-style plate.
CCNode* plateRow(float width, float height) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = kit::makePlate(width, height)) {
        plate->setPosition({0.f, 0.f});
        row->addChild(plate, -1);
    }
    return row;
}

// Level-editor arrow with a common fallback.
CCSprite* editorArrow(char const* editorFrame, float rotationFallback) {
    if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(editorFrame)) {
        return spr;
    }
    auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (spr) spr->setRotation(rotationFallback);
    return spr;
}

CCLabelBMFont* titleLabel(char const* text, float maxW, float scale = 0.40f) {
    auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kit::kTitleColor);
    l->limitLabelWidth(maxW, scale, 0.1f);
    return l;
}

CCLabelBMFont* descLabel(char const* text, float wrapW, float scale = 0.44f) {
    auto* l = CCLabelBMFont::create(text, "chatFont.fnt", wrapW / scale, kCCTextAlignmentLeft);
    l->setScale(scale);
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kit::kDescColor);
    return l;
}

float scaledHeight(CCLabelBMFont* l) {
    return l ? l->getContentSize().height * l->getScale() : 0.f;
}

}

CCTexture2D* checkerTexture() {
    static Ref<CCTexture2D> cached = nullptr;
    if (cached) return cached;

    constexpr int kSide = 32;
    constexpr int kCell = 8;
    ts::ImageBuffer buffer(kSide, kSide);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            bool dark = ((x / kCell) + (y / kCell)) % 2 == 0;
            std::uint8_t v = dark ? 46 : 62;
            buffer.setAt(x, y, {v, v, static_cast<std::uint8_t>(v + 8), 255});
        }
    }
    cached = ts::SpritePreviewRenderer::createTexture(buffer);
    return cached;
}

CCNode* makeSwatch(float size, ccColor3B color, bool selected) {
    auto* node = CCNode::create();
    node->setAnchorPoint({0.5f, 0.5f});
    node->setContentSize({size, size});

    if (auto* ring = paimon::SpriteHelper::createColorPanel(
            size, size, selected ? ccColor3B{255, 255, 255} : ccColor3B{0, 0, 0},
            selected ? 255 : 130, 4.f)) {
        ring->setAnchorPoint({0.f, 0.f});
        ring->setPosition({0.f, 0.f});
        node->addChild(ring, -1);
    }

    float inset = selected ? 3.f : 2.f;
    if (auto* fill = paimon::SpriteHelper::createColorPanel(
            size - inset * 2.f, size - inset * 2.f, color, 255, 3.f)) {
        fill->setAnchorPoint({0.f, 0.f});
        fill->setPosition({inset, inset});
        node->addChild(fill);
    }
    return node;
}


int zoneChipsPerRow(float width, int zoneCount) {
    constexpr float kMinChipW = 52.f;
    int const fitting = static_cast<int>(
        (width + kZoneChipGap) / (kMinChipW + kZoneChipGap));
    return std::clamp(fitting, 1, std::max(zoneCount, 1));
}

float zoneChipsHeight(float width, int zoneCount) {
    int const perRow = zoneChipsPerRow(width, zoneCount);
    int const rows = std::max(1, (zoneCount + perRow - 1) / perRow);
    return static_cast<float>(rows) * kZoneChipH +
           static_cast<float>(rows - 1) * kZoneChipGap;
}

CCNode* makeZoneChips(float width, std::vector<ZoneChip> const& zones,
                      int selected, std::function<void(int)> onSelect) {
    constexpr float kChipH = kZoneChipH;
    constexpr float kGap = kZoneChipGap;

    int const n = static_cast<int>(zones.size());
    float const barH = zoneChipsHeight(width, n);

    auto* bar = CCNode::create();
    bar->setAnchorPoint({0.f, 0.f});
    bar->setContentSize({width, barH});
    if (zones.empty()) return bar;

    auto* menu = rowMenu(bar);

    int const perRow = zoneChipsPerRow(width, n);
    float const chipW =
        (width - kGap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < n; ++i) {
        auto const& zone = zones[static_cast<std::size_t>(i)];
        bool isSelected = i == selected;

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({chipW, kChipH});

// Vanilla-style zone tabs.
        auto* face = kit::makeTabFace(zone.label.c_str(), isSelected, chipW, kChipH);
        if (face) {
            face->setPosition({chipW / 2.f, kChipH / 2.f});
            holder->addChild(face);
        }

// Mark zones that already contain content.
        if (zone.layerCount > 0) {
            auto faceSize = face ? face->getScaledContentSize() : CCSize{chipW, kChipH};
            if (auto* dot = paimon::SpriteHelper::createColorPanel(
                    6.f, 6.f, zone.accent, 255, 3.f)) {
                dot->setAnchorPoint({0.f, 0.f});
                dot->setPosition({chipW / 2.f + faceSize.width / 2.f - 9.f,
                                  kChipH / 2.f + faceSize.height / 2.f - 9.f});
                holder->addChild(dot, 3);
            }
        }

        int const col = i % perRow;
        int const line = i / perRow;
        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [cb, i](CCMenuItemSpriteExtra*) { if (*cb) (*cb)(i); });
        btn->setPosition({
            chipW / 2.f + static_cast<float>(col) * (chipW + kGap),
            barH - kChipH / 2.f - static_cast<float>(line) * (kChipH + kGap),
        });
        menu->addChild(btn);
    }

    return bar;
}


CCNode* makeLayerRow(float width, LayerRowSpec spec) {
    constexpr float kRowH = 34.f;
    constexpr float kThumb = 26.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

// Green marks the active layer, matching GD's active-state color.
    if (auto* panel = kit::makePlate(width, kRowH,
            spec.selected ? ccColor3B{150, 240, 160} : ccColor3B{255, 255, 255},
            spec.selected ? 255 : 200)) {
        panel->setPosition({0.f, 0.f});
        row->addChild(panel, -1);
    }

    auto* menu = rowMenu(row);

// Eye toggle compares with and without the layer.
    float x = 16.f;
    if (auto* eye = CCSprite::createWithSpriteFrameName(
            spec.visible ? "GJ_checkOn_001.png" : "GJ_checkOff_001.png")) {
        eye->setScale(0.42f);
        auto* btn = CCMenuItemExt::createSpriteExtra(eye,
            [cb = spec.onToggleVisible](CCMenuItemSpriteExtra*) { if (cb) cb(); });
        btn->setPosition({x, kRowH / 2.f});
        menu->addChild(btn);
    }
    x += 18.f;

// Thumbnail and name share one large selection target.
    float const nameW = width - x - 96.f;
    auto* hit = CCNode::create();
    hit->setAnchorPoint({0.f, 0.5f});
    hit->setContentSize({kThumb + 4.f + nameW, kRowH - 4.f});

    if (auto* swatch = makeSwatch(kThumb, spec.swatch, false)) {
        swatch->setPosition({kThumb / 2.f, (kRowH - 4.f) / 2.f});
        hit->addChild(swatch);
    }

    auto* nameLbl = CCLabelBMFont::create(spec.name.c_str(), "bigFont.fnt");
    nameLbl->setAnchorPoint({0.f, 0.f});
    nameLbl->setColor(spec.selected ? ccColor3B{26, 40, 30} : ccColor3B{255, 255, 255});
    nameLbl->limitLabelWidth(nameW, 0.36f, 0.12f);
    nameLbl->setPosition({kThumb + 6.f, (kRowH - 4.f) / 2.f + 1.f});
    hit->addChild(nameLbl);

    if (!spec.subtitle.empty()) {
        auto* subLbl = CCLabelBMFont::create(spec.subtitle.c_str(), "chatFont.fnt");
        subLbl->setAnchorPoint({0.f, 1.f});
        subLbl->setScale(0.36f);
        subLbl->setColor(spec.selected ? ccColor3B{56, 84, 62} : kit::kDescColor);
        subLbl->limitLabelWidth(nameW / 0.36f, 0.36f, 0.14f);
        subLbl->setPosition({kThumb + 6.f, (kRowH - 4.f) / 2.f});
        hit->addChild(subLbl);
    }

    auto* selectBtn = CCMenuItemExt::createSpriteExtra(hit,
        [cb = spec.onSelect](CCMenuItemSpriteExtra*) { if (cb) cb(); });
    selectBtn->setAnchorPoint({0.f, 0.5f});
    selectBtn->setPosition({x, kRowH / 2.f});
    menu->addChild(selectBtn);

// Right-aligned order arrows and overflow menu.
    auto addArrow = [&](float px, bool up, bool enabled, std::function<void()> action) {
        auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (!spr) return;
        spr->setScale(0.36f);
        spr->setRotation(up ? 90.f : -90.f);
        if (!enabled) spr->setOpacity(70);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [enabled, action](CCMenuItemSpriteExtra*) { if (enabled && action) action(); });
        btn->setPosition({px, kRowH / 2.f});
        menu->addChild(btn);
    };
    addArrow(width - 62.f, true, spec.canMoveUp, spec.onMoveUp);
    addArrow(width - 42.f, false, spec.canMoveDown, spec.onMoveDown);

    if (auto* more = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png")) {
        more->setScale(0.32f);
        auto* btn = CCMenuItemExt::createSpriteExtra(more,
            [cb = spec.onMore](CCMenuItemSpriteExtra*) { if (cb) cb(); });
        btn->setPosition({width - 18.f, kRowH / 2.f});
        menu->addChild(btn);
    }

    return row;
}


CCNode* makeSwatchGrid(float width, std::vector<ccColor3B> const& colors,
                       ccColor4B current, std::function<void(ccColor3B)> onPick,
                       std::function<void()> onCustom) {
    constexpr float kSwatch = 22.f;
    constexpr float kGap = 4.f;
    constexpr float kPadX = 10.f;

    int perRow = std::max(1, static_cast<int>(
        (width - kPadX * 2.f + kGap) / (kSwatch + kGap)));
    int total = static_cast<int>(colors.size()) + 1;  // Includes "Other".
    int rows = (total + perRow - 1) / perRow;

    float rowH = static_cast<float>(rows) * (kSwatch + kGap) - kGap + 12.f;

    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});
    node->setContentSize({width, rowH});

    auto* menu = rowMenu(node);
    auto pick = std::make_shared<std::function<void(ccColor3B)>>(std::move(onPick));

    for (int i = 0; i < total; ++i) {
        int col = i % perRow;
        int line = i / perRow;
        float cx = kPadX + kSwatch / 2.f + static_cast<float>(col) * (kSwatch + kGap);
        float cy = rowH - 6.f - kSwatch / 2.f - static_cast<float>(line) * (kSwatch + kGap);

        if (i == static_cast<int>(colors.size())) {
// Open the full color wheel.
            auto* holder = CCNode::create();
            holder->setAnchorPoint({0.5f, 0.5f});
            holder->setContentSize({kSwatch, kSwatch});
            if (auto* panel = paimon::SpriteHelper::createColorPanel(
                    kSwatch, kSwatch, {70, 76, 96}, 235, 4.f)) {
                panel->setAnchorPoint({0.f, 0.f});
                holder->addChild(panel);
            }
            auto* plus = CCLabelBMFont::create("...", "bigFont.fnt");
            plus->setScale(0.34f);
            plus->setAnchorPoint({0.5f, 0.5f});
            plus->setPosition({kSwatch / 2.f, kSwatch / 2.f + 2.f});
            holder->addChild(plus);

            auto* btn = CCMenuItemExt::createSpriteExtra(holder,
                [cb = onCustom](CCMenuItemSpriteExtra*) { if (cb) cb(); });
            btn->setPosition({cx, cy});
            menu->addChild(btn);
            continue;
        }

        auto color = colors[static_cast<std::size_t>(i)];
        bool selected = color.r == current.r && color.g == current.g && color.b == current.b;
        auto* swatch = makeSwatch(kSwatch, color, selected);
        auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
            [pick, color](CCMenuItemSpriteExtra*) { if (*pick) (*pick)(color); });
        btn->setPosition({cx, cy});
        menu->addChild(btn);
    }

    return node;
}

CCNode* makeColorRow(float width, char const* title, char const* desc,
                     ccColor4B color, std::function<void()> onPress) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;
    float textMaxW = width - 70.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }
    float rowH = std::max(kPad + kTitleH + descH + kPad, 36.f);

    auto* row = plateRow(width, rowH);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);
    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* menu = rowMenu(row);
    auto* swatch = makeSwatch(26.f, {color.r, color.g, color.b}, false);
    auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
        [cb = std::move(onPress)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
    btn->setPosition({width - 24.f, rowH / 2.f});
    menu->addChild(btn);

    return row;
}


CCNode* makeGradientRow(float width, GradientSpec const& spec, char const* buttonText,
                        std::function<void()> onEdit) {
    constexpr float kStripH = 20.f;
    constexpr float kRowH = 60.f;
    float const stripW = width - 22.f;

    auto* row = plateRow(width, kRowH);

// Draw the strip left-to-right so it reflects color order, not gradient angle.
    GradientSpec stripSpec = spec;
    stripSpec.kind = GradientKind::Linear;
    stripSpec.angleDeg = 0.f;
    auto buffer = GradientRasterizer::rasterize(256, 24, stripSpec);
    if (auto* texture = ts::SpritePreviewRenderer::createTexture(buffer)) {
        auto* strip = CCSprite::createWithTexture(texture);
        if (strip) {
            strip->setAnchorPoint({0.f, 1.f});
            strip->setScaleX(stripW / 256.f);
            strip->setScaleY(kStripH / 24.f);
            strip->setPosition({11.f, kRowH - 7.f});
            row->addChild(strip);
        }
    }

    auto* kindLbl = CCLabelBMFont::create(
        spec.kind == GradientKind::Radial
            ? fmt::format("Radial  -  {} colores", spec.stops.size()).c_str()
            : fmt::format("Lineal {}deg  -  {} colores",
                          static_cast<int>(spec.angleDeg), spec.stops.size()).c_str(),
        "chatFont.fnt");
    kindLbl->setAnchorPoint({0.f, 0.5f});
    kindLbl->setScale(0.42f);
    kindLbl->setColor(kit::kDescColor);
    kindLbl->setPosition({10.f, kRowH - kStripH - 18.f});
    row->addChild(kindLbl);

    auto* menu = rowMenu(row);
    auto* spr = ButtonSprite::create(buttonText, "goldFont.fnt", "GJ_button_04.png", 0.7f);
    if (spr) spr->setScale(0.5f);
    auto* btn = CCMenuItemExt::createSpriteExtra(spr,
        [cb = std::move(onEdit)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
    btn->setPosition({width - 14.f - btn->getScaledContentSize().width / 2.f,
                      kRowH - kStripH - 18.f});
    menu->addChild(btn);

    return row;
}


CCNode* makeDualButtonRow(float width,
                          char const* leftText, std::function<void()> onLeft,
                          char const* rightText, std::function<void()> onRight) {
    constexpr float kRowH = 34.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* menu = rowMenu(row);

    auto add = [&](char const* text, float cx, char const* sprite,
                   std::function<void()> action) {
        auto* spr = ButtonSprite::create(text, "goldFont.fnt", sprite, 0.7f);
        if (!spr) return;
        spr->setScale(0.55f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        btn->setPosition({cx, kRowH / 2.f});
        menu->addChild(btn);
    };
    add(leftText, width * 0.27f, "GJ_button_01.png", std::move(onLeft));
    add(rightText, width * 0.73f, "GJ_button_04.png", std::move(onRight));

    return row;
}

CCNode* makeEmptyState(float width, char const* title, char const* desc) {
    auto* titleLbl = CCLabelBMFont::create(title, "bigFont.fnt");
    titleLbl->setAnchorPoint({0.5f, 1.f});
    titleLbl->limitLabelWidth(width - 24.f, 0.42f, 0.15f);
    titleLbl->setColor({235, 240, 250});

    auto* descLbl = CCLabelBMFont::create(
        desc, "chatFont.fnt", (width - 30.f) / 0.44f, kCCTextAlignmentCenter);
    descLbl->setScale(0.44f);
    descLbl->setAnchorPoint({0.5f, 1.f});
    descLbl->setColor(kit::kDescColor);

    float rowH = scaledHeight(titleLbl) + scaledHeight(descLbl) + 22.f;

    auto* row = plateRow(width, rowH);

    titleLbl->setPosition({width / 2.f, rowH - 8.f});
    row->addChild(titleLbl);
    descLbl->setPosition({width / 2.f, rowH - 8.f - scaledHeight(titleLbl) - 4.f});
    row->addChild(descLbl);

    return row;
}

CCNode* makeNudgePad(float width, char const* title, char const* desc,
                     std::function<void(float, float)> onNudge,
                     std::function<void()> onCenter) {
    constexpr float kRowH = 78.f;
// Nudge by one sixtieth of the canvas per tap.
    constexpr float kStep = 1.f / 60.f;

    auto* row = plateRow(width, kRowH);

    auto* titleLbl = titleLabel(title, width - 110.f);
    titleLbl->setPosition({11.f, kRowH - 7.f});
    row->addChild(titleLbl);

    if (desc && desc[0] != '\0') {
        auto* descLbl = descLabel(desc, width - 110.f);
        descLbl->setPosition({11.f, kRowH - 23.f});
        row->addChild(descLbl);
    }

    auto* menu = rowMenu(row);
    auto nudge = std::make_shared<std::function<void(float, float)>>(std::move(onNudge));

    float const padCX = width - 44.f;
    float const padCY = kRowH / 2.f;

// Reuse the editor's movement arrows.
    struct Arrow { float dx, dy; char const* frame; float fallbackRotation; };
    constexpr Arrow kArrows[] = {
        {-1.f,  0.f, "edit_leftBtn_001.png",    0.f},
        { 1.f,  0.f, "edit_rightBtn_001.png", 180.f},
        { 0.f,  1.f, "edit_upBtn_001.png",     90.f},
        { 0.f, -1.f, "edit_downBtn_001.png",  -90.f},
    };
    for (auto const& arrow : kArrows) {
        auto* spr = editorArrow(arrow.frame, arrow.fallbackRotation);
        if (!spr) continue;
        spr->setScale(0.62f);
        float dx = arrow.dx * kStep;
        float dy = arrow.dy * kStep;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [nudge, dx, dy](CCMenuItemSpriteExtra*) { if (*nudge) (*nudge)(dx, dy); });
        btn->setPosition({padCX + arrow.dx * 22.f, padCY + arrow.dy * 20.f});
        menu->addChild(btn);
    }

    auto* centerSpr = ButtonSprite::create("Centrar", "goldFont.fnt", "GJ_button_04.png", 0.7f);
    if (centerSpr) {
        centerSpr->setScale(0.42f);
        auto* btn = CCMenuItemExt::createSpriteExtra(centerSpr,
            [cb = std::move(onCenter)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
        btn->setPosition({padCX, padCY});
        menu->addChild(btn);
    }

    return row;
}

}
