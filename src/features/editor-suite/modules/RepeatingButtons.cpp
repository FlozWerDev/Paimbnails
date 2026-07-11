#include "../EditorModule.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <array>
#include <string_view>

#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

float delaySeconds() {
    auto const delay = moduleSetting<int64_t>("editor-mod-repeat-delay-ms", 500);
    return static_cast<float>(std::clamp<int64_t>(delay, 50, 5000)) / 1000.f;
}

float rateSeconds() {
    auto const rate = moduleSetting<int64_t>("editor-mod-repeat-rate-ms", 100);
    return static_cast<float>(std::clamp<int64_t>(rate, 16, 2000)) / 1000.f;
}

bool hasRepeatableAncestor(CCMenuItemSpriteExtra* item, EditorUI* ui) {
    if (!item || !ui) return false;
    if (item == ui->m_undoBtn || item == ui->m_redoBtn
        || item == ui->m_layerNextBtn || item == ui->m_layerPrevBtn) {
        return true;
    }

    constexpr std::array<std::string_view, 4> repeatableMenus{
        "edit-menu",
        "zoom-menu",
        "custom-move-menu",
        "hjfod.betteredit/custom-move-menu",
    };

    for (auto* node = item->getParent(); node; node = node->getParent()) {
        if (node == ui->m_editButtonBar) return true;
        auto const id = std::string_view(node->getID());
        if (std::ranges::find(repeatableMenus, id) != repeatableMenus.end()) return true;
        if (node == ui) break;
    }
    return false;
}

} // namespace

class $modify(PaimonRepeatButton, CCMenuItemSpriteExtra) {
    struct Fields {
        bool holding = false;
        bool repeated = false;
    };

    bool canRepeat() const {
        if (!moduleEnabled("editor-mod-repeating-buttons") || !paimon::isEditorScene()) {
            return false;
        }
        return hasRepeatableAncestor(const_cast<PaimonRepeatButton*>(this), EditorUI::get());
    }

    void resetRepeated(float) {
        m_fields->repeated = false;
    }

    void fireRepeat(float) {
        if (!m_fields->holding || !canRepeat() || !isRunning()) {
            m_fields->holding = false;
            this->unschedule(schedule_selector(PaimonRepeatButton::fireRepeat));
            return;
        }
        if (!m_pListener || !m_pfnSelector) return;

        m_fields->repeated = true;
        (m_pListener->*m_pfnSelector)(this);
        if (!m_animationEnabled) setScale(m_baseScale);
    }

    $override
    void selected() {
        CCMenuItemSpriteExtra::selected();
        if (!canRepeat()) return;

        m_fields->holding = true;
        m_fields->repeated = false;
        this->unschedule(schedule_selector(PaimonRepeatButton::fireRepeat));
        this->schedule(
            schedule_selector(PaimonRepeatButton::fireRepeat),
            rateSeconds(),
            kCCRepeatForever,
            delaySeconds()
        );
    }

    $override
    void unselected() {
        CCMenuItemSpriteExtra::unselected();
        m_fields->holding = false;
        this->unschedule(schedule_selector(PaimonRepeatButton::fireRepeat));
        if (m_fields->repeated) {
            this->scheduleOnce(schedule_selector(PaimonRepeatButton::resetRepeated), 0.f);
        }
    }

    $override
    void activate() {
        if (m_fields->repeated) {
            m_fields->repeated = false;
            return;
        }
        CCMenuItemSpriteExtra::activate();
    }
};
