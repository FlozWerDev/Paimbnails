#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <algorithm>

#include "../core/Settings.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"

using namespace geode::prelude;

// Preserves buttons' original scale. Can't migrate to MenuItemActivatedEvent
// (Geode 5.6.0): that event is observational only, but this hook modifies
// behavior (captures/restores scale and avoids an activate() crash).
class $modify(PaimonMenuItemScaleFix, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // VeryLate so we don't clobber other mods.
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::selected", geode::Priority::VeryLate);
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::unselected", geode::Priority::VeryLate);
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::activate", geode::Priority::VeryLate);
    }

    struct Fields {
        float m_originalScaleX = 1.0f;
        float m_originalScaleY = 1.0f;
        bool m_scaleCaptured = false;
    };

    bool shouldSmoothThisButton() {
        if (!paimon::settings::smoothui::enabled()) return false;
        if (paimon::settings::smoothui::reducedMotion()) return false;
        if (!paimon::settings::smoothui::animateButtons()) return false;

        auto scope = paimon::settings::smoothui::buttonScope();
        if (scope == "off") return false;
        if (scope == "paimon") return PaimonButtonHighlighter::isRegisteredButton(this);
        return true;
    }

    float smoothSpeed() {
        return static_cast<float>(std::clamp(
            paimon::settings::smoothui::globalSpeed(), 0.35, 2.5));
    }

    void captureScaleIfNeeded() {
        if (!m_fields->m_scaleCaptured) {
            m_fields->m_originalScaleX = this->getScaleX();
            m_fields->m_originalScaleY = this->getScaleY();
            m_fields->m_scaleCaptured = true;
        }
    }

    $override
    void selected() {
        bool smooth = shouldSmoothThisButton();

        if (PaimonButtonHighlighter::isRegisteredButton(this) || smooth) {
            captureScaleIfNeeded();
        }

        CCMenuItemSpriteExtra::selected();

        if (smooth) {
            auto strength = static_cast<float>(std::clamp(
                paimon::settings::smoothui::motionStrength(), 0.0, 2.0));
            auto pressScale = static_cast<float>(std::clamp(
                paimon::settings::smoothui::buttonPressScale(), 0.80, 1.05));
            pressScale = 1.f + (pressScale - 1.f) * strength;

            this->stopAllActions();
            this->setScaleX(m_fields->m_originalScaleX);
            this->setScaleY(m_fields->m_originalScaleY);
            auto action = CCScaleTo::create(
                std::max(0.025f, 0.075f / smoothSpeed()),
                m_fields->m_originalScaleX * pressScale,
                m_fields->m_originalScaleY * pressScale
            );
            this->runAction(CCEaseSineOut::create(action));
        }
    }

    $override
    void unselected() {
        CCMenuItemSpriteExtra::unselected();

        bool smooth = shouldSmoothThisButton();

        if (PaimonButtonHighlighter::isRegisteredButton(this) || smooth) {
            if (m_fields->m_scaleCaptured) {
                this->stopAllActions();
                float speed = smooth ? smoothSpeed() : 1.f;
                if (smooth && paimon::settings::smoothui::buttonReleaseBounce()) {
                    auto strength = static_cast<float>(std::clamp(
                        paimon::settings::smoothui::motionStrength(), 0.0, 2.0));
                    float over = 1.f + 0.045f * strength;
                    this->runAction(CCSequence::create(
                        CCEaseSineOut::create(CCScaleTo::create(
                            std::max(0.025f, 0.070f / speed),
                            m_fields->m_originalScaleX * over,
                            m_fields->m_originalScaleY * over
                        )),
                        CCEaseSineInOut::create(CCScaleTo::create(
                            std::max(0.025f, 0.115f / speed),
                            m_fields->m_originalScaleX,
                            m_fields->m_originalScaleY
                        )),
                        nullptr
                    ));
                } else {
                    auto scaleTo = CCScaleTo::create(
                        std::max(0.025f, 0.16f / speed),
                        m_fields->m_originalScaleX,
                        m_fields->m_originalScaleY
                    );
                    this->runAction(CCEaseSineOut::create(scaleTo));
                }
            }
        }
    }

    $override
    void activate() {
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            // Safety guard for Paimbnails-owned buttons that may lack a target/selector
            // (recycled or created programmatically). Still call the original so we don't
            // cut other mods' hook chain; vanilla activate() tolerates null selectors.
            if ((!this->m_pListener || !this->m_pfnSelector) && this->m_nScriptTapHandler == 0) {
                log::warn("[MenuItemScaleFix] Paimbnails button without target/selector - passing through to original");
                CCMenuItemSpriteExtra::activate();
                return;
            }

            // The button callback may close or rebuild the current popup; retain
            // the item so restoring the scale doesn't touch freed memory.
            bool restoreScale = m_fields->m_scaleCaptured;
            float originalScaleX = m_fields->m_originalScaleX;
            float originalScaleY = m_fields->m_originalScaleY;
            this->retain();
            CCMenuItemSpriteExtra::activate();

            if (restoreScale) {
                this->stopAllActions();
                this->setScaleX(originalScaleX);
                this->setScaleY(originalScaleY);
            }
            this->release();
        } else {
            // Non-Paimbnails button: no modification, just call the original.
            CCMenuItemSpriteExtra::activate();
        }
    }
};
