#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

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
        float m_originalScale = 1.0f;
        bool m_scaleCaptured = false;
    };

    $override
    void selected() {
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (!m_fields->m_scaleCaptured) {
                m_fields->m_originalScale = this->getScale();
                m_fields->m_scaleCaptured = true;
            }
        }

        CCMenuItemSpriteExtra::selected();
    }

    $override
    void unselected() {
        CCMenuItemSpriteExtra::unselected();

        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                this->stopAllActions();
                auto scaleTo = CCScaleTo::create(0.2f, m_fields->m_originalScale);
                this->runAction(CCEaseSineOut::create(scaleTo));
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
                log::warn("[MenuItemScaleFix] Paimbnails button without target/selector — passing through to original");
                CCMenuItemSpriteExtra::activate();
                return;
            }

            // The button callback may close or rebuild the current popup; retain
            // the item so restoring the scale doesn't touch freed memory.
            bool restoreScale = m_fields->m_scaleCaptured;
            float originalScale = m_fields->m_originalScale;
            this->retain();
            CCMenuItemSpriteExtra::activate();

            if (restoreScale) {
                this->stopAllActions();
                this->setScale(originalScale);
            }
            this->release();
        } else {
            // Non-Paimbnails button: no modification, just call the original.
            CCMenuItemSpriteExtra::activate();
        }
    }
};
