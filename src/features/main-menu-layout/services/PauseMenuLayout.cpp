#include "PauseMenuLayout.hpp"
#include "PauseMenuGrid.hpp"
#include "../../../utils/PauseMenuNodes.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../framework/compat/ModCompat.hpp"

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace paimon::menu_layout {
namespace {

struct PauseAction {
    std::string_view id;
    char const* english;
    char const* spanish;
};

// preserve the original nodes, callbacks, tags and mode-dependent availability.
constexpr std::array kActions = {
    PauseAction{"death-tracker-button", "Deaths", "Muertes"},
    PauseAction{"death-markers-button", "Markers", "Marcas"},
    PauseAction{"comments-button", "Comments", "Comentarios"},
    PauseAction{"quests-button", "Quests", "Misiones"},
    PauseAction{"ignore-damage-button", "No Damage", "Sin dano"},
    PauseAction{"platformer-ui-button", "Controls", "Controles"},
    PauseAction{"options-button", "Options", "Opciones"},
    PauseAction{"hide-pause-menu-button", "Hide", "Ocultar"},
    PauseAction{"speed-modifier-button", "Speed", "Velocidad"},
    PauseAction{"practice-ui-button", "Practice UI", "UI Practica"},
    PauseAction{"thumbnail-capture-button"_spr, "Capture", "Capturar"},
    PauseAction{"thumbnail-select-button"_spr, "Upload", "Subir"},
    PauseAction{"paimon-progressbar-config-button"_spr, "Progress", "Progreso"},
    PauseAction{"death-animation-button"_spr, "Death VFX", "VFX Muerte"},
    PauseAction{"death-effects-button"_spr, "Death SFX", "SFX Muerte"},
    PauseAction{"web-request-feedback-button"_spr, "Feedback", "Feedback"},
};

template <typename PauseT>
CCNode* platformSettingsButtonOf(PauseT* pause) {
    if (!pause) return nullptr;
    if constexpr (requires(PauseT* p) { p->m_platformSettingsButton; }) {
        return pause->m_platformSettingsButton;
    }
    return nullptr;
}

template <typename PauseT, typename Fn>
void forEachNativeSlider(PauseT* pause, Fn&& fn) {
    if (!pause) return;
    if constexpr (requires(PauseT* p) { p->m_musicSlider; p->m_sfxSlider; }) {
        fn(static_cast<CCNode*>(pause->m_musicSlider));
        fn(static_cast<CCNode*>(pause->m_sfxSlider));
    }
}

auto actionFor(CCNode* node, PauseLayer* pause = nullptr) {
    if (!node) return kActions.end();
    auto it = std::ranges::find(kActions, node->getID(), &PauseAction::id);
    if (it != kActions.end()) return it;
    if (pause && node == platformSettingsButtonOf(pause)) {
        return std::ranges::find(kActions, std::string_view{"platformer-ui-button"}, &PauseAction::id);
    }
    return kActions.end();
}

void addCaption(CCMenuItem* item, PauseMenuSlot const& slot, bool spanish, PauseLayer* pause) {
    auto const action = actionFor(item, pause);
    if (action == kActions.end()) return;

    auto* label = typeinfo_cast<CCLabelBMFont*>(item->getChildByID("pause-action-caption"_spr));
    if (!label) {
        label = CCLabelBMFont::create("", "bigFont.fnt");
        if (!label) return;
        label->setID("pause-action-caption"_spr);
        item->addChild(label);
    }
    label->setVisible(slot.showCaption);
    label->setString(spanish ? action->spanish : action->english);
    float const scale = std::max(.01f, std::abs(item->getScale()));
    label->limitLabelWidth(slot.captionWidth / scale, .2f / scale, .1f / scale);
    label->setColor({220, 230, 245});
    label->setRotation(-item->getRotation());
    // account for rotated capture icons and for the native menu's anchor point.
    auto* parent = item->getParent();
    auto const local = item->convertToNodeSpace(parent->getParent()->convertToWorldSpace(
        {slot.x, slot.y - slot.iconSize * .5f - 7.f}));
    label->setPosition(local);
}

struct SidebarClearance {
    bool hasButtons = false;
    float innerEdge = 0.f;
};

SidebarClearance arrangeSidebar(PauseLayer* pause, CCMenu* menu, CCSize size, bool rightSide) {
    if (!menu) return {};
    std::vector<CCMenuItem*> buttons;
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) {
            buttons.push_back(item);
        }
    }
    std::stable_sort(buttons.begin(), buttons.end(), [pause](auto* left, auto* right) {
        return actionFor(left, pause) < actionFor(right, pause);
    });
    auto const slots = pauseMenuSlots(size.width, size.height, buttons.size(), rightSide);
    if (slots.empty()) return {};

    // leave the menu's origin and each button's parent intact for saved layouts.
    menu->setLayout(nullptr);
    bool const spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
    float innerEdge = rightSide ? size.width : 0.f;
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        auto* item = buttons[index];
        auto const& slot = slots[index];
        item->setPosition(menu->convertToNodeSpace(menu->getParent()->convertToWorldSpace({slot.x, slot.y})));
        auto const bounds = item->getContentSize();
        float const extent = std::max(bounds.width, bounds.height);
        if (extent > 0.f) {
            float const scale = std::min(1.f, slot.iconSize / extent);
            item->setScale(scale);
            if (auto* extra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) extra->m_baseScale = scale;
        }
        addCaption(item, slot, spanish, pause);

        float const halfExtent = std::max(slot.iconSize, slot.showCaption ? slot.captionWidth : 0.f) * .5f;
        if (rightSide) {
            innerEdge = std::min(innerEdge, slot.x - halfExtent);
        } else {
            innerEdge = std::max(innerEdge, slot.x + halfExtent);
        }
    }
    return {true, innerEdge};
}

struct WorldYRange {
    bool valid = false;
    float bottom = 0.f;
    float top = 0.f;
};

WorldYRange measureWorldYRange(CCNode* node) {
    if (!node || !node->isVisible()) return {};
    auto const cs = node->getContentSize();
    float const sy = std::abs(node->getScaleY());
    // Nodes like Slider or progress bars sometimes have zero contentSize on
    // the wrapper while holding visible sprites/bars inside.
    if (cs.height > 1.f && sy > .001f) {
        auto const p0 = node->convertToWorldSpace({0.f, 0.f});
        auto const p1 = node->convertToWorldSpace({0.f, cs.height});
        auto const p2 = node->convertToWorldSpace({cs.width, 0.f});
        auto const p3 = node->convertToWorldSpace({cs.width, cs.height});
        float const lo = std::min({p0.y, p1.y, p2.y, p3.y});
        float const hi = std::max({p0.y, p1.y, p2.y, p3.y});
        if (std::isfinite(lo) && std::isfinite(hi)) {
            return {true, lo, hi};
        }
    }
    WorldYRange combined;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        auto const sub = measureWorldYRange(child);
        if (!sub.valid) continue;
        if (!combined.valid) {
            combined = sub;
        } else {
            combined.bottom = std::min(combined.bottom, sub.bottom);
            combined.top = std::max(combined.top, sub.top);
        }
    }
    if (combined.valid) return combined;
    // Fallback for wrappers with neither contentSize nor measurable children:
    // anchor a conservative ~16px band around the node's world origin.
    auto const origin = node->convertToWorldSpace({0.f, 0.f});
    if (!std::isfinite(origin.y)) return {};
    return {true, origin.y - 8.f, origin.y + 8.f};
}

bool isUpperObstacleId(std::string_view id) {
    constexpr std::array<std::string_view, 18> kUpperIds = {
        "normal-progress-bar",
        "practice-progress-bar",
        "practice-mode-bar",
        "progress-bar",
        "normal-progress-label",
        "practice-progress-label",
        "normal-mode-label",
        "practice-mode-label",
        "normal-mode-percentage",
        "practice-mode-percentage",
        "progress-bar-label",
        "level-name",
        "platformer-title",
        "time-label",
        "points-label",
        "time-button",
        "points-button",
        "play-time",
    };
    return std::ranges::find(kUpperIds, id) != kUpperIds.end();
}

bool isLowerObstacleId(std::string_view id) {
    constexpr std::array<std::string_view, 9> kLowerIds = {
        "music-slider",
        "sfx-slider",
        "music-label",
        "sfx-label",
        "startpos-container",
        "bottom-button-menu",
        "coin-container",
        "versus-pause-menu",
        "show-ui-menu",
    };
    return std::ranges::find(kLowerIds, id) != kLowerIds.end();
}

struct VerticalBand {
    float bottom;
    float top;
};

VerticalBand computeCenterVerticalBand(PauseLayer* pause, CCSize size,
    CCMenu* leftMenu, CCMenu* rightMenu, CCMenu* centerMenu) {
    float bandBottom = std::min(84.f, size.height * .26f);
    float bandTop = size.height - std::min(68.f, size.height * .22f);

    auto inspectNode = [&](CCNode* node) {
        if (!node || !node->isVisible()) return;
        if (node == leftMenu || node == rightMenu || node == centerMenu) return;
        auto const id = node->getID();
        bool const upper = isUpperObstacleId(id);
        bool const lower = isLowerObstacleId(id);
        if (!upper && !lower) return;

        auto const range = measureWorldYRange(node);
        if (!range.valid) return;
        float const mid = (range.bottom + range.top) * .5f;

        // Guard against mispositioned/full-screen nodes by requiring the
        // obstacle to live in its expected half of the pause layer.
        if (upper && range.bottom > size.height * .38f) {
            bandTop = std::min(bandTop, range.bottom - 8.f);
        } else if (lower && range.top < size.height * .52f && mid < size.height * .48f) {
            bandBottom = std::max(bandBottom, range.top + 8.f);
        }
    };

    auto scanContainer = [&](CCNode* container) {
        if (!container) return;
        for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
            inspectNode(child);
        }
    };

    scanContainer(pause);
    if (auto* mainLayer = pause->getChildByID("main-layer")) {
        scanContainer(mainLayer);
    }

    // Also respect GD's native slider / platformer settings members if present
    // without node IDs or parented outside standard menus.
    forEachNativeSlider(pause, [&](CCNode* slider) {
        if (!slider || !slider->isVisible()) return;
        auto const range = measureWorldYRange(slider);
        if (range.valid && range.top < size.height * .52f) {
            bandBottom = std::max(bandBottom, range.top + 8.f);
        }
    });
    if (auto* platBtn = platformSettingsButtonOf(pause)) {
        auto* pParent = platBtn->getParent();
        if (platBtn->isVisible() && pParent != leftMenu && pParent != rightMenu && pParent != centerMenu) {
            auto const range = measureWorldYRange(platBtn);
            if (range.valid) {
                float const mid = (range.bottom + range.top) * .5f;
                if (mid > size.height * .55f && range.bottom > size.height * .38f) {
                    bandTop = std::min(bandTop, range.bottom - 8.f);
                } else if (mid < size.height * .45f && range.top < size.height * .52f) {
                    bandBottom = std::max(bandBottom, range.top + 8.f);
                }
            }
        }
    }

    // Guarantee a minimum usable band even on unusually cramped layouts.
    if (bandTop - bandBottom < 36.f) {
        float const fallbackMid = std::clamp((bandBottom + bandTop) * .5f, 44.f, size.height - 44.f);
        bandBottom = fallbackMid - 20.f;
        bandTop = fallbackMid + 20.f;
    }
    return {bandBottom, bandTop};
}

void arrangePrimaryActions(PauseLayer* pause, CCMenu* menu, CCSize size,
    SidebarClearance leftClear, SidebarClearance rightClear,
    CCMenu* leftMenu, CCMenu* rightMenu) {
    if (!menu) return;
    // Keep platformer full-restart grouped with retry, and support both
    // practice/normal mode buttons and checkpoint retry across classic/platformer.
    constexpr std::array<std::string_view, 8> order = {
        "full-restart-button",
        "retry-button",
        "practice-retry-button",
        "play-button",
        "practice-button",
        "normal-button",
        "edit-button",
        "exit-button"
    };
    std::vector<CCMenuItem*> buttons;
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) buttons.push_back(item);
    }
    if (buttons.empty()) return;
    std::stable_sort(buttons.begin(), buttons.end(), [&](auto* left, auto* right) {
        return std::ranges::find(order, left->getID()) < std::ranges::find(order, right->getID());
    });

    auto const band = computeCenterVerticalBand(pause, size, leftMenu, rightMenu, menu);
    float const bandHeight = std::max(32.f, band.top - band.bottom);
    float const maxButtonSize = std::clamp(bandHeight - 4.f, 32.f, 62.f);

    float const leftBound = leftClear.hasButtons ? (leftClear.innerEdge + 10.f) : 16.f;
    float const rightBound = rightClear.hasButtons ? (rightClear.innerEdge - 10.f) : (size.width - 16.f);
    float const available = std::max(50.f, rightBound - leftBound);

    float totalWidth = 0.f;
    for (auto* item : buttons) {
        auto const bounds = item->getContentSize();
        float const scale = maxButtonSize / std::max(1.f, std::max(bounds.width, bounds.height));
        totalWidth += bounds.width * std::min(1.f, scale);
    }
    float const gap = buttons.size() > 1
        ? std::min(10.f, available / static_cast<float>(buttons.size()) * .15f)
        : 0.f;
    float const fit = std::min(1.f, (available - gap * (buttons.size() - 1)) / std::max(1.f, totalWidth));
    float const rowWidth = totalWidth * fit + gap * (buttons.size() - 1);
    float const centerX = (leftBound + rightBound) * .5f;
    float x = centerX - rowWidth * .5f;

    float maxActualHeight = 0.f;
    for (auto* item : buttons) {
        auto const bounds = item->getContentSize();
        float const scale = fit * std::min(1.f, maxButtonSize / std::max(1.f, std::max(bounds.width, bounds.height)));
        maxActualHeight = std::max(maxActualHeight, bounds.height * scale);
    }
    float const halfHeight = maxActualHeight * .5f;
    float const preferredY = size.height * .5f + 2.f;
    float const minCenterY = band.bottom + halfHeight;
    float const maxCenterY = band.top - halfHeight;
    float const centerY = minCenterY <= maxCenterY
        ? std::clamp(preferredY, minCenterY, maxCenterY)
        : (band.bottom + band.top) * .5f;

    menu->setLayout(nullptr);
    for (auto* item : buttons) {
        auto const bounds = item->getContentSize();
        float const scale = fit * std::min(1.f, maxButtonSize / std::max(1.f, std::max(bounds.width, bounds.height)));
        float const width = bounds.width * scale;
        item->setScale(scale);
        if (auto* extra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) extra->m_baseScale = scale;
        item->setPosition(menu->convertToNodeSpace(menu->getParent()->convertToWorldSpace(
            {x + width * .5f, centerY})));
        x += width + gap;
    }
}

} // namespace

bool arrangePauseMenu(PauseLayer* pause) {
    if (!pause || compat::ModCompat::isCompactPauseMenuLoaded()) return false;
    auto const size = CCDirector::get()->getWinSize();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 320.f || size.height < 180.f) {
        return false;
    }
    auto* centerMenu = findPauseButtonMenu(pause, "center-button-menu");
    if (!centerMenu) return false;
    auto* leftMenu = findPauseButtonMenu(pause, "left-button-menu");
    auto* rightMenu = findPauseButtonMenu(pause, "right-button-menu");
    auto const leftClear = arrangeSidebar(pause, leftMenu, size, false);
    auto const rightClear = arrangeSidebar(pause, rightMenu, size, true);
    arrangePrimaryActions(pause, centerMenu, size, leftClear, rightClear, leftMenu, rightMenu);
    return true;
}

} // namespace paimon::menu_layout
