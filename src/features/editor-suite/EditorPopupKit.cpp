#include "EditorPopupKit.hpp"

#include "../../ui/PaimonUI.hpp"
#include "../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace geode::prelude;

namespace paimon::editor::kit {

namespace {

constexpr float kWrapFloor = 0.14f;

CCSprite* loadSprite(char const* paim, char const* fallback) {
    if (paim && *paim) {
        auto const expanded = Mod::get()->expandSpriteName(paim);
        if (auto* sprite = SpriteHelper::safeCreateWithFrameName(expanded.c_str())) return sprite;
        if (auto* sprite = SpriteHelper::safeCreate(expanded.c_str())) return sprite;
    }
    if (fallback && *fallback) {
        if (auto* sprite = SpriteHelper::safeCreateWithFrameName(fallback)) return sprite;
    }
    auto* empty = CCSprite::create();
    empty->setContentSize({16.f, 16.f});
    return empty;
}

bool isGold(char const* font) {
    return font && std::string_view(font) == "goldFont.fnt";
}

// bmfont glyphs sit below the centre of their line box.
float labelLift(char const* font, float scale) {
    return (isGold(font) ? 2.f : 1.2f) * scale / 0.5f;
}

float plateMultiplier(float height) {
    return std::clamp(height / 36.f, 0.4f, 0.8f);
}

NineSlice* plate(char const* texture, CCSize size) {
    auto* slice = NineSlice::create(texture);
    if (!slice) return nullptr;
    slice->setScaleMultiplier(plateMultiplier(size.height));
    slice->setContentSize(size);
    slice->setAnchorPoint({0.f, 0.f});
    slice->setPosition({0.f, 0.f});
    return slice;
}

EditorButtonSprite* toolSprite(char const* paim, char const* fallback, EditorBaseColor color,
    float size, ccColor3B glyphColor) {
    auto* top = loadSprite(paim, fallback);
    top->setColor(glyphColor);
    auto* sprite = EditorButtonSprite::create(top, color);
    if (!sprite) return nullptr;
    // the base fits the top edge to edge; gd's own editor icons leave a margin.
    sprite->setTopRelativeScale(0.8f);
    auto const content = sprite->getContentSize();
    float const largest = std::max(content.width, content.height);
    if (largest > 0.f) sprite->setScale(size / largest);
    return sprite;
}

void layoutPill(Pill& pill) {
    if (!pill.body || !pill.label) return;
    auto const size = pill.body->getContentSize();
    bool const hasIcon = pill.icon != nullptr;
    float const iconSize = hasIcon ? size.height * 0.62f : 0.f;
    float const gap = hasIcon ? 3.f : 0.f;
    float const room = std::max(8.f, size.width - size.height * 0.7f - iconSize - gap);
    pill.label->limitLabelWidth(room, pill.labelScale, pill.labelScale * 0.45f);

    float const textWidth = pill.label->getScaledContentSize().width;
    float x = (size.width - iconSize - gap - textWidth) * 0.5f;
    float const y = size.height * 0.5f + (pill.background ? 1.f : 0.f);
    if (hasIcon) {
        limitNodeSize(pill.icon, {iconSize, iconSize}, 4.f, 0.01f);
        pill.icon->setPosition({x + iconSize * 0.5f, y});
        x += iconSize + gap;
    }
    pill.label->setAnchorPoint({0.f, 0.5f});
    float const lift = labelLift(pill.goldFont ? "goldFont.fnt" : "bigFont.fnt", pill.label->getScale());
    pill.label->setPosition({x, y + lift});
}

} // namespace

CCSprite* glyph(char const* paim, char const* fallback, float size, ccColor3B color) {
    auto* sprite = loadSprite(paim, fallback);
    limitNodeSize(sprite, {size, size}, 4.f, 0.01f);
    sprite->setColor(color);
    return sprite;
}

CCMenuItemSpriteExtra* toolButton(char const* paim, char const* fallback, EditorBaseColor color,
    float size, std::function<void()> onPress) {
    auto* sprite = toolSprite(paim, fallback, color, size, tint::white);
    if (!sprite) return nullptr;
    return CCMenuItemExt::createSpriteExtra(sprite, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
}

CCMenuItemToggler* toolToggle(char const* paim, char const* fallback, EditorBaseColor onColor,
    float size, bool value, std::function<void(bool)> onChange) {
    auto* on = toolSprite(paim, fallback, onColor, size, tint::white);
    auto* off = toolSprite(paim, fallback, EditorBaseColor::Gray, size, tint::off);
    if (!on || !off) return nullptr;
    // the callback runs before the toggler flips its own state.
    auto* toggle = CCMenuItemExt::createToggler(on, off,
        [cb = std::move(onChange)](CCMenuItemToggler* item) {
            if (cb) cb(!item->isToggled());
        });
    toggle->toggle(value);
    return toggle;
}

CCMenuItemSpriteExtra* arrowButton(bool right, float height, std::function<void()> onPress) {
    auto* sprite = loadSprite(nullptr, "edit_leftBtn_001.png");
    float const natural = sprite->getContentSize().height;
    if (natural > 0.f) sprite->setScale(height / natural);
    sprite->setFlipX(right);
    auto* button = CCMenuItemExt::createSpriteExtra(sprite, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
    button->setSizeMult(1.6f);
    return button;
}

Pill pill(char const* paim, char const* fallback, char const* text, char const* texture,
    CCSize size, std::function<void()> onPress, char const* font, ccColor3B iconColor) {
    Pill result;
    result.iconColor = iconColor;
    result.goldFont = isGold(font);
    result.labelScale = size.height * (isGold(font) ? 0.019f : 0.0165f);

    auto* body = CCNode::create();
    body->setContentSize(size);
    body->setAnchorPoint({0.5f, 0.5f});
    result.body = body;

    if (texture) {
        result.background = plate(texture, size);
        if (result.background) body->addChild(result.background, -1);
    }
    if (paim || fallback) {
        result.icon = loadSprite(paim, fallback);
        result.icon->setColor(iconColor);
        body->addChild(result.icon, 1);
    }
    result.label = label(text, font, result.labelScale);
    body->addChild(result.label, 1);
    layoutPill(result);

    result.button = CCMenuItemExt::createSpriteExtra(body, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
    return result;
}

void setPillText(Pill& pill, std::string const& text) {
    if (!pill.label) return;
    pill.label->setString(text.c_str());
    layoutPill(pill);
}

void setPillIcon(Pill& pill, char const* paim, char const* fallback) {
    if (!pill.body) return;
    if (pill.icon) pill.icon->removeFromParent();
    pill.icon = loadSprite(paim, fallback);
    pill.icon->setColor(pill.iconColor);
    pill.body->addChild(pill.icon, 1);
    layoutPill(pill);
}

void setPillSkin(Pill& pill, char const* texture) {
    if (!pill.body || !texture) return;
    if (pill.background) pill.background->removeFromParent();
    pill.background = plate(texture, pill.body->getContentSize());
    if (pill.background) pill.body->addChild(pill.background, -1);
}

NineSlice* inset(CCRect const& area, GLubyte opacity) {
    auto* panel = paimon::ui::makeInset(area.size, opacity);
    panel->setPosition(area.origin);
    return panel;
}

CCNode* header(char const* paim, char const* fallback, char const* text, float width) {
    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});
    node->setContentSize({width, 20.f});

    auto* icon = glyph(paim, fallback, 13.f, tint::gold);
    icon->setPosition({9.f, 10.f});
    node->addChild(icon);

    auto* title = label(text, "goldFont.fnt", 0.5f);
    title->limitLabelWidth(width - 24.f, 0.5f, 0.2f);
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({19.f, 10.f + labelLift("goldFont.fnt", title->getScale())});
    node->addChild(title);

    auto* line = paimon::ui::makeDivider(width, tint::gold, 90);
    line->setPosition({width * 0.5f, 0.f});
    node->addChild(line);
    return node;
}

CCLabelBMFont* stepper(CCNode* parent, CCMenu* menu, StepperSpec const& spec, CCRect const& row,
    bool stripe, std::function<void(int)> onStep) {
    float const x = row.origin.x;
    float const width = row.size.width;
    float const height = row.size.height;
    float const y = row.origin.y + height * 0.5f;
    float const button = std::min(15.f, height - 3.f);

    if (stripe) {
        auto* band = CCLayerColor::create({0, 0, 0, 28}, width, height);
        band->setPosition(row.origin);
        parent->addChild(band);
    }

    auto* icon = glyph(spec.icon, spec.fallback, std::min(13.f, height - 5.f), spec.color);
    icon->setPosition({x + 11.f, y});
    parent->addChild(icon);

    float const plusX = x + width - 9.f;
    float const boxX = plusX - button * 0.5f - 3.f - kValueBoxWidth * 0.5f;
    float const minusX = boxX - kValueBoxWidth * 0.5f - 3.f - button * 0.5f;

    auto* title = label(spec.title, "bigFont.fnt", 0.3f, tint::label);
    title->limitLabelWidth((minusX - button * 0.5f - 4.f) - (x + 21.f), 0.3f, 0.16f);
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({x + 21.f, y + 0.6f});
    parent->addChild(title);

    auto* box = NineSlice::create("square02b_001.png");
    box->setScaleMultiplier(0.3f);
    box->setContentSize({kValueBoxWidth, std::min(14.f, height - 4.f)});
    box->setColor({0, 0, 0});
    box->setOpacity(120);
    box->setPosition({boxX, y});
    parent->addChild(box);

    auto* value = label("-", "goldFont.fnt", 0.36f);
    value->setPosition({boxX, y + labelLift("goldFont.fnt", 0.36f)});
    parent->addChild(value, 1);

    auto shared = std::make_shared<std::function<void(int)>>(std::move(onStep));
    for (int direction : {-1, 1}) {
        auto* sprite = direction < 0
            ? glyph("paim_ui_stepMinus.png", "edit_leftBtn_001.png", button)
            : glyph("paim_ui_stepPlus.png", "GJ_plus2Btn_001.png", button);
        auto* item = CCMenuItemExt::createSpriteExtra(sprite, [shared, direction](auto*) {
            if (*shared) (*shared)(direction);
        });
        item->setSizeMult(1.3f);
        item->setPosition({direction < 0 ? minusX : plusX, y});
        menu->addChild(item);
    }
    return value;
}

void setValue(CCLabelBMFont* value, std::string const& text) {
    if (!value) return;
    value->setString(text.c_str());
    value->limitLabelWidth(kValueBoxWidth - 6.f, 0.36f, 0.2f);
}

CCNode* checker(CCSize size, float cell, ccColor3B dark, ccColor3B light) {
    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});
    node->setContentSize(size);
    node->addChild(CCLayerColor::create({dark.r, dark.g, dark.b, 255}, size.width, size.height));

    auto* cells = CCDrawNode::create();
    ccColor4F const fill = ccc4FFromccc3B(light);
    int const columns = static_cast<int>(std::ceil(size.width / cell));
    int const rows = static_cast<int>(std::ceil(size.height / cell));
    for (int row = 0; row < rows; ++row) {
        for (int column = row % 2; column < columns; column += 2) {
            float const x0 = column * cell;
            float const y0 = row * cell;
            float const x1 = std::min(size.width, x0 + cell);
            float const y1 = std::min(size.height, y0 + cell);
            CCPoint corners[4]{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            cells->drawPolygon(corners, 4, fill, 0.f, fill);
        }
    }
    node->addChild(cells);
    return node;
}

CCLabelBMFont* label(char const* text, char const* font, float scale, ccColor3B color) {
    auto* result = CCLabelBMFont::create(text ? text : "", font);
    result->setScale(scale);
    result->setColor(color);
    return result;
}

void setWrapped(CCLabelBMFont* label, std::string const& text, CCSize box, float maxScale,
    float minSingle) {
    if (!label) return;
    label->setWidth(kCCLabelAutomaticWidth);
    label->setString(text.c_str());
    auto size = label->getContentSize();
    float const single = std::min({
        maxScale,
        box.width / std::max(size.width, 1.f),
        box.height / std::max(size.height, 1.f),
    });
    if (single >= minSingle) {
        label->setScale(single);
        return;
    }
    float scale = maxScale;
    for (; scale > kWrapFloor; scale -= 0.01f) {
        label->setWidth(box.width / scale);
        size = label->getContentSize();
        if (size.width * scale <= box.width && size.height * scale <= box.height) break;
    }
    label->setScale(std::max(scale, kWrapFloor));
}

} // namespace paimon::editor::kit
