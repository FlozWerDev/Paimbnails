#include "PauseMenuLayout.hpp"
#include "PauseMenuGrid.hpp"
#include "../../../utils/PauseMenuNodes.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../framework/compat/ModCompat.hpp"

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <array>
#include <string_view>

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
    PauseAction{"practice-ui-button", "Practice", "Practica"},
    PauseAction{"thumbnail-capture-button"_spr, "Capture", "Capturar"},
    PauseAction{"thumbnail-select-button"_spr, "Upload", "Subir"},
    PauseAction{"paimon-progressbar-config-button"_spr, "Progress", "Progreso"},
    PauseAction{"death-animation-button"_spr, "Death VFX", "VFX Muerte"},
    PauseAction{"death-effects-button"_spr, "Death SFX", "SFX Muerte"},
    PauseAction{"web-request-feedback-button"_spr, "Feedback", "Feedback"},
};

auto actionFor(CCNode* node) {
    return std::ranges::find(kActions, node->getID(), &PauseAction::id);
}

void addCaption(CCMenuItem* item, PauseMenuSlot const& slot, bool spanish) {
    auto const action = actionFor(item);
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

void arrangeSidebar(CCMenu* menu, CCSize size, bool rightSide) {
    if (!menu) return;
    std::vector<CCMenuItem*> buttons;
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) {
            buttons.push_back(item);
        }
    }
    std::stable_sort(buttons.begin(), buttons.end(), [](auto* left, auto* right) {
        return actionFor(left) < actionFor(right);
    });
    auto const slots = pauseMenuSlots(size.width, size.height, buttons.size(), rightSide);
    if (slots.empty()) return;

    // leave the menu's origin and each button's parent intact for saved layouts.
    menu->setLayout(nullptr);
    bool const spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
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
        addCaption(item, slot, spanish);
    }
}

void arrangePrimaryActions(CCMenu* menu, CCSize size) {
    if (!menu) return;
    constexpr std::array<std::string_view, 6> order = {
        "retry-button", "play-button", "practice-button", "edit-button", "full-restart-button", "exit-button"
    };
    std::vector<CCMenuItem*> buttons;
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) buttons.push_back(item);
    }
    if (buttons.empty()) return;
    std::stable_sort(buttons.begin(), buttons.end(), [&](auto* left, auto* right) {
        return std::ranges::find(order, left->getID()) < std::ranges::find(order, right->getID());
    });

    float totalWidth = 0.f;
    for (auto* item : buttons) {
        auto const bounds = item->getContentSize();
        float const scale = 62.f / std::max(1.f, std::max(bounds.width, bounds.height));
        totalWidth += bounds.width * std::min(1.f, scale);
    }
    float const available = std::max(50.f, size.width - 216.f);
    float const gap = std::min(10.f, available / static_cast<float>(buttons.size()) * .15f);
    float const fit = std::min(1.f, (available - gap * (buttons.size() - 1)) / std::max(1.f, totalWidth));
    float x = (size.width - totalWidth * fit - gap * (buttons.size() - 1)) * .5f;
    menu->setLayout(nullptr);
    for (auto* item : buttons) {
        auto const bounds = item->getContentSize();
        float const scale = fit * std::min(1.f, 62.f / std::max(1.f, std::max(bounds.width, bounds.height)));
        float const width = bounds.width * scale;
        item->setScale(scale);
        if (auto* extra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) extra->m_baseScale = scale;
        item->setPosition(menu->convertToNodeSpace(menu->getParent()->convertToWorldSpace(
            {x + width * .5f, size.height * .5f + 2.f})));
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
    if (!findPauseButtonMenu(pause, "center-button-menu")) return false;
    arrangeSidebar(findPauseButtonMenu(pause, "left-button-menu"), size, false);
    arrangeSidebar(findPauseButtonMenu(pause, "right-button-menu"), size, true);
    arrangePrimaryActions(findPauseButtonMenu(pause, "center-button-menu"), size);
    return true;
}

} // namespace paimon::menu_layout
