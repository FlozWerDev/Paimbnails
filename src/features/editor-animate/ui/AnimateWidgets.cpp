#include "AnimateWidgets.hpp"

#include "../AnimateText.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace paimon::animate::widgets {

ccColor3B clipColor(int index) {
    static constexpr std::array<ccColor3B, kColorTags> palette = {{
        {40, 200, 255},
        {255, 160, 40},
        {120, 230, 90},
        {255, 110, 190},
        {175, 125, 255},
        {255, 220, 70},
        {255, 95, 95},
        {60, 220, 190},
    }};
    return palette[static_cast<std::size_t>(((index % kColorTags) + kColorTags) % kColorTags)];
}

ccColor3B scaled(ccColor3B color, float factor) {
    auto channel = [factor](GLubyte value) {
        return static_cast<GLubyte>(std::clamp(static_cast<float>(value) * factor, 0.f, 255.f));
    };
    return {channel(color.r), channel(color.g), channel(color.b)};
}

CCSprite* icon(char const* paim, std::initializer_list<char const*> fallbacks) {
    if (paim && *paim) {
        auto const expanded = Mod::get()->expandSpriteName(paim);
        if (auto* sprite = SpriteHelper::safeCreateWithFrameName(expanded.c_str())) return sprite;
    }
    for (auto const* frame : fallbacks) {
        if (auto* sprite = SpriteHelper::safeCreateWithFrameName(frame)) return sprite;
    }
    auto* empty = CCSprite::create();
    empty->setContentSize({16.f, 16.f});
    return empty;
}

CCSprite* fitted(CCSprite* sprite, float size) {
    if (sprite) limitNodeSize(sprite, {size, size}, 4.f, 0.01f);
    return sprite;
}

EditorButtonSprite* toolSprite(CCSprite* top, EditorBaseColor color, float size) {
    auto* sprite = EditorButtonSprite::create(top, color);
    if (!sprite) return nullptr;
    auto const content = sprite->getContentSize();
    float const largest = std::max(content.width, content.height);
    if (largest > 0.f) sprite->setScale(size / largest);
    return sprite;
}

CCMenuItemSpriteExtra* toolButton(
    CCSprite* top, EditorBaseColor color, float size, std::function<void()> onPress
) {
    auto* sprite = toolSprite(top, color, size);
    if (!sprite) return nullptr;
    return CCMenuItemExt::createSpriteExtra(sprite, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
}

CCMenuItemSpriteExtra* iconButton(CCSprite* sprite, float size, std::function<void()> onPress) {
    fitted(sprite, size);
    auto* button = CCMenuItemExt::createSpriteExtra(sprite, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
    button->setSizeMult(1.4f);
    return button;
}

std::string seconds(float value) {
    return value < 10.f ? fmt::format("{:.2f}s", value) : fmt::format("{:.1f}s", value);
}

char const* modeName(PlayMode mode) {
    switch (mode) {
        case PlayMode::Loop: return tr("Loop", "Bucle");
        case PlayMode::Once: return tr("Once", "Una vez");
        case PlayMode::PingPong: return tr("Ping-pong", "Ida y vuelta");
        case PlayMode::Repeat: return tr("Repeat N", "Repetir N");
        case PlayMode::Reverse: return tr("Reverse loop", "Bucle inverso");
    }
    return "";
}

char const* modeIcon(PlayMode mode) {
    switch (mode) {
        case PlayMode::Loop: return "paim_anim_loop.png";
        case PlayMode::Once: return "paim_anim_once.png";
        case PlayMode::PingPong: return "paim_anim_pingpong.png";
        case PlayMode::Repeat: return "paim_anim_repeat.png";
        case PlayMode::Reverse: return "paim_anim_reverse.png";
    }
    return "paim_anim_loop.png";
}

char const* endName(EndAction end) {
    switch (end) {
        case EndAction::HoldLast: return tr("Hold last frame", "Mantener el ultimo");
        case EndAction::HideAll: return tr("Hide everything", "Ocultar todo");
        case EndAction::ShowFirst: return tr("Back to first frame", "Volver al primero");
    }
    return "";
}

void notifyError(std::string const& text) {
    PaimonNotify::show(text, NotificationIcon::Error, 2.5f);
}

void notifyOk(std::string const& text) {
    PaimonNotify::show(text, NotificationIcon::Success, 1.6f);
}

} // namespace paimon::animate::widgets
