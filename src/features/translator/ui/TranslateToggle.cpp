#include "TranslateToggle.hpp"

#include "TranslateButton.hpp"
#include "../TranslationService.hpp"
#include "../TranslatorConfig.hpp"
#include "../TranslatorText.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/ui/Notification.hpp>
#include <utility>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate {

TranslateToggle* TranslateToggle::create(std::string original,
    std::function<void(std::string const&)> apply) {
    auto* toggle = new TranslateToggle();
    if (toggle->setup(std::move(original), std::move(apply))) {
        toggle->autorelease();
        return toggle;
    }
    delete toggle;
    return nullptr;
}

bool TranslateToggle::setup(std::string original, std::function<void(std::string const&)> apply) {
    if (!CCNode::init()) return false;

    m_original = std::move(original);
    m_apply = std::move(apply);
    setID("paimon-translate-toggle"_spr);
    setContentSize({30.f, 30.f});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({30.f, 30.f});
    addChild(menu);

    auto* button = paimon::translate::ui::makeTranslateButton([this] { onPressed(); }, .5f);
    if (!button) return false;
    button->setID("paimon-translate-toggle-btn"_spr);
    menu->addChild(button);
    m_button = button;

    updateVisual();
    return true;
}

void TranslateToggle::onPressed() {
    if (m_busy) return;
    auto settings = loadSettings();
    if (!settings.enabled) return;

    if (m_showing) {
        restore();
        return;
    }
    if (!m_translated.empty() && m_target == settings.target) {
        if (m_apply) m_apply(m_translated);
        m_showing = true;
        updateVisual();
        return;
    }
    request(false);
}

void TranslateToggle::translate(bool silent) {
    if (m_busy || m_showing) return;
    if (!loadSettings().enabled) return;
    request(silent);
}

void TranslateToggle::restore() {
    if (m_apply) m_apply(m_original);
    m_showing = false;
    updateVisual();
}

void TranslateToggle::request(bool silent) {
    auto settings = loadSettings();
    auto text = sanitizeForTranslation(m_original);
    if (!isTranslatable(text)) {
        if (!silent) {
            Notification::create(
                paimon::loc::tr("translator.empty", "There is no text to translate"),
                NotificationIcon::Info, 1.8f)->show();
        }
        return;
    }

    m_busy = true;
    auto const requestId = ++m_request;
    updateVisual();

    WeakRef<TranslateToggle> safeThis = this;
    TranslationService::get().translate(text, settings.target,
        [safeThis, requestId, target = settings.target, silent](bool ok, std::string result) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto ref = safeThis.lock();
            auto* self = static_cast<TranslateToggle*>(ref.data());
            if (!self || self->m_request != requestId) return;

            self->m_busy = false;
            if (!ok || result.empty()) {
                self->updateVisual();
                if (!silent) {
                    Notification::create(
                        paimon::loc::tr("translator.failed", "Translation failed"),
                        NotificationIcon::Error, 2.2f)->show();
                }
                return;
            }

            self->m_translated = std::move(result);
            self->m_target = target;
            self->m_showing = true;
            if (self->m_apply) self->m_apply(self->m_translated);
            self->updateVisual();
        });
}

void TranslateToggle::updateVisual() {
    if (!m_button) return;
    ui::setTranslateButtonState(m_button,
        m_busy      ? ui::TranslateButtonState::Busy
        : m_showing ? ui::TranslateButtonState::Showing
                    : ui::TranslateButtonState::Idle);
}

}
