#pragma once
// icon-maker widgets built on paimon::configkit: one clear decision per row;
// complex controls open a popup.

#include "../data/FillSpec.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::icon_maker::ui {

// section accent shared by cards and chips.
constexpr cocos2d::ccColor3B kAccentZones   = {120, 190, 255};
constexpr cocos2d::ccColor3B kAccentLayers  = {150, 200, 255};
constexpr cocos2d::ccColor3B kAccentPaint   = {255, 150, 205};
constexpr cocos2d::ccColor3B kAccentShape   = {135, 235, 165};
constexpr cocos2d::ccColor3B kAccentProject = {255, 214, 120};
constexpr cocos2d::ccColor3B kAccentDanger  = {255, 120, 120};

// reusable transparency board for previews.
cocos2d::CCTexture2D* checkerTexture();
// drop it on gl reload; the texture dies with the context.
void resetCheckerTexture();

// rounded color swatch.
cocos2d::CCNode* makeSwatch(float size, cocos2d::ccColor3B color, bool selected);

struct ZoneChip {
    std::string label;
    cocos2d::ccColor3B accent{255, 255, 255};
    int layerCount = 0;
    // zone thumbnail; same texture the canvas draws, so no extra memory.
    cocos2d::CCTexture2D* preview = nullptr;
};

constexpr float kZoneChipH = 30.f;
constexpr float kZoneChipGap = 3.f;

// wrap tabs only when the available width would make them unreadable.
float zoneChipsHeight(float width, int zoneCount);

// icon-zone tabs, rebuilt when selection changes.
cocos2d::CCNode* makeZoneChips(float width, std::vector<ZoneChip> const& zones,
                               int selected, std::function<void(int)> onSelect);

struct LayerRowSpec {
    std::string name;
    std::string subtitle;                       // display subtitle.
    cocos2d::ccColor3B swatch{255, 255, 255};   // fallback while the thumb renders.
    cocos2d::CCTexture2D* thumb = nullptr;
    bool visible = true;
    bool locked = false;
    bool selected = false;
    bool canMoveUp = false;
    bool canMoveDown = false;
    std::function<void()> onSelect;
    std::function<void()> onToggleVisible;
    std::function<void()> onToggleLock;
    std::function<void()> onMoveUp;
    std::function<void()> onMoveDown;
    std::function<void()> onMore;
};

cocos2d::CCNode* makeLayerRow(float width, LayerRowSpec spec);

// quick-color grid plus an "other" button.
cocos2d::CCNode* makeSwatchGrid(float width,
                                std::vector<cocos2d::ccColor3B> const& colors,
                                cocos2d::ccColor4B current,
                                std::function<void(cocos2d::ccColor3B)> onPick,
                                std::function<void()> onCustom);

// current gradient strip and full-editor button.
cocos2d::CCNode* makeGradientRow(float width, GradientSpec const& spec,
                                 char const* buttonText,
                                 std::function<void()> onEdit);

// text on the left and a clickable color swatch on the right.
cocos2d::CCNode* makeColorRow(float width, char const* title, char const* desc,
                              cocos2d::ccColor4B color,
                              std::function<void()> onPress);

// two equal-weight side-by-side actions.
cocos2d::CCNode* makeDualButtonRow(float width,
                                   char const* leftText, std::function<void()> onLeft,
                                   char const* rightText, std::function<void()> onRight);

// small square preview of a texture on a transparency board.
cocos2d::CCNode* makeThumb(float size, cocos2d::CCTexture2D* texture,
                           cocos2d::ccColor3B fallback);

// "#rrggbb" field plus a live swatch.
cocos2d::CCNode* makeHexRow(float width, cocos2d::ccColor4B current,
                            std::function<void(cocos2d::ccColor3B)> onChange);

// frozen values; the editor maps them onto the guide box.
enum class AlignMode : int {
    Left = 0, CenterH = 1, Right = 2,
    Top = 3, CenterV = 4, Bottom = 5,
};

// six one-tap alignments against the recommended icon box.
cocos2d::CCNode* makeAlignRow(float width, char const* title, char const* desc,
                              std::function<void(AlignMode)> onAlign);

// centered empty-state message.
cocos2d::CCNode* makeEmptyState(float width, char const* title, char const* desc);

// four-arrow nudge control.
cocos2d::CCNode* makeNudgePad(float width, char const* title, char const* desc,
                              std::function<void(float dx, float dy)> onNudge,
                              std::function<void()> onCenter);

}
