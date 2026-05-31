#include <Geode/Geode.hpp>
#include <Geode/modify/Slider.hpp>
#include <Geode/modify/SliderTouchLogic.hpp>

#include "../services/CustomSliderManager.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

// ────────────────────────────────────────────────────────────
// We store a back-pointer from the SliderThumb to the Slider
// via UserObject so the SliderTouchLogic hook can find it.
// ────────────────────────────────────────────────────────────

#define PAIMON_SLIDER_KEY "paimon-slider-ref"

// Simple ref holder
class PaimonSliderRef : public CCObject {
public:
    Slider* m_slider = nullptr;
    PaimonSliderRef(Slider* s) : m_slider(s) { this->autorelease(); }
};

// ────────────────────────────────────────────────────────────
// Hook Slider: replace thumb images with custom icon
// ────────────────────────────────────────────────────────────

class $modify(PaimonSlider, Slider) {
    struct Fields {
        bool m_isAffected = false;
        float m_oldValue = 0.f;
        Ref<CCNode> m_normalNode = nullptr;  // normal state container
        Ref<CCNode> m_selectedNode = nullptr; // selected state container
    };

    $override
    bool init(CCNode* target, SEL_MenuHandler handler, char const* bar,
              char const* groove, char const* thumb, char const* thumbSel,
              float scale) {
        if (!Slider::init(target, handler, bar, groove, thumb, thumbSel, scale))
            return false;

        auto& mgr = CustomSliderManager::get();
        if (!mgr.config().enabled) return true;

        // Schedule for next frame so the slider is fully parented
        this->scheduleOnce(
            schedule_selector(PaimonSlider::applyIconDeferred), 0.f);

        return true;
    }

    void applyIconDeferred(float) {
        auto& mgr = CustomSliderManager::get();
        if (!mgr.config().enabled) return;

        // Check if this slider should be affected
        if (!mgr.shouldAffectSlider(this)) return;

        auto* thumbNode = this->getThumb();
        if (!thumbNode) return;

        m_fields->m_isAffected = true;

        // Store back-reference on the thumb so SliderTouchLogic can find us
        thumbNode->setUserObject(PAIMON_SLIDER_KEY, new PaimonSliderRef(this));

        upgradeSlider(thumbNode);
    }

    void upgradeSlider(SliderThumb* thumb) {
        auto& mgr = CustomSliderManager::get();
        auto& cfg = mgr.config();
        auto thumbSize = thumb->getContentSize();

        // Create normal state image (static)
        auto* normalBase = CCSprite::create();
        normalBase->setContentSize(thumbSize);
        auto* normalNode = CCSprite::create();
        normalNode->setScale(0.9f);
        normalBase->addChild(normalNode);
        normalNode->setPosition(thumbSize / 2.f);
        mgr.addIconToNode(normalNode, false);

        // Create selected state image (on press/drag)
        auto* selectedBase = CCSprite::create();
        selectedBase->setContentSize(thumbSize);
        auto* selectedNode = CCSprite::create();
        selectedNode->setScale(0.9f);
        selectedBase->addChild(selectedNode);
        selectedNode->setPosition(thumbSize / 2.f);
        mgr.addIconToNode(selectedNode, true);

        // Enable cascade opacity
        setCascadeOpacityDeep(normalBase);
        setCascadeOpacityDeep(selectedBase);

        // REPLACE the thumb's normal and selected images
        thumb->setNormalImage(normalBase);
        thumb->setSelectedImage(selectedBase);

        // Store nodes for animation
        m_fields->m_normalNode = normalNode;
        m_fields->m_selectedNode = selectedNode;
    }

    $override
    void setValue(float value) {
        Slider::setValue(value);
        // No-op for animation on programmatic set
    }

    // ── Animation methods called by SliderTouchLogic hook ──

    void onDragBegin() {
        auto& cfg = CustomSliderManager::get().config();
        if (!cfg.animateOnDrag) return;

        auto* node = m_fields->m_selectedNode.data();
        if (!node) return;

        // Scale up (bounce) animation
        if (cfg.animType == SliderAnimType::Bounce || cfg.animType == SliderAnimType::BounceRotate) {
            node->stopAllActions();
            float targetScale = 0.9f * cfg.animBounceScale;
            node->runAction(CCEaseBackOut::create(
                CCScaleTo::create(cfg.animDuration * 0.5f, targetScale)));
        }

        // Reset rotation
        if (cfg.animType == SliderAnimType::Rotate || cfg.animType == SliderAnimType::BounceRotate) {
            node->setRotation(0.f);
        }

        m_fields->m_oldValue = this->getValue();
    }

    void onDragMove() {
        auto& cfg = CustomSliderManager::get().config();
        if (!cfg.animateOnDrag) return;

        auto* node = m_fields->m_selectedNode.data();
        if (!node) return;

        if (cfg.animType == SliderAnimType::Rotate || cfg.animType == SliderAnimType::BounceRotate) {
            float speed = this->getValue() - m_fields->m_oldValue;
            int sign = speed >= 0 ? 1 : -1;
            float maxAngle = cfg.animRotateDeg;
            float angle = sign * maxAngle * std::min(1.f, std::abs(speed) * 50.f);

            float dur = cfg.animDuration * 0.3f;
            node->runAction(CCSequence::create(
                CCRotateTo::create(dur, angle), nullptr));

            // Auto-reset rotation when slider stops
            node->stopActionByTag(42);
            auto* resetAction = CCSequence::create(
                CCDelayTime::create(dur),
                CCRotateTo::create(dur * 2.f, 0.f), nullptr);
            resetAction->setTag(42);
            node->runAction(resetAction);
        }

        m_fields->m_oldValue = this->getValue();
    }

    void onDragEnd() {
        auto& cfg = CustomSliderManager::get().config();
        if (!cfg.animateOnDrag) return;

        auto* node = m_fields->m_selectedNode.data();
        if (!node) return;

        // Scale back down
        if (cfg.animType == SliderAnimType::Bounce || cfg.animType == SliderAnimType::BounceRotate) {
            node->stopAllActions();
            node->runAction(CCEaseBackOut::create(
                CCScaleTo::create(cfg.animDuration, 0.9f)));
        }

        // Reset rotation
        if (cfg.animType == SliderAnimType::Rotate || cfg.animType == SliderAnimType::BounceRotate) {
            node->stopActionByTag(42);
            node->runAction(CCEaseBackOut::create(
                CCRotateTo::create(cfg.animDuration, 0.f)));
        }
    }

    // Helper to enable cascade opacity recursively
    static void setCascadeOpacityDeep(CCNode* node) {
        if (auto* spr = typeinfo_cast<CCSprite*>(node)) {
            spr->setCascadeOpacityEnabled(true);
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                setCascadeOpacityDeep(child);
            }
        }
    }
};

// ────────────────────────────────────────────────────────────
// Hook SliderTouchLogic to trigger animations
// We use the UserObject on the thumb to find the parent Slider.
// ────────────────────────────────────────────────────────────

class $modify(PaimonSliderTouch, SliderTouchLogic) {
    PaimonSlider* getMySlider() {
        if (!m_thumb) return nullptr;
        auto* ref = static_cast<PaimonSliderRef*>(m_thumb->getUserObject(PAIMON_SLIDER_KEY));
        if (!ref || !ref->m_slider) return nullptr;
        return static_cast<PaimonSlider*>(ref->m_slider);
    }

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        bool result = SliderTouchLogic::ccTouchBegan(touch, event);
        if (result) {
            if (auto* slider = getMySlider()) {
                if (slider->m_fields->m_isAffected) {
                    slider->onDragBegin();
                }
            }
        }
        return result;
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchMoved(touch, event);
        if (auto* slider = getMySlider()) {
            if (slider->m_fields->m_isAffected) {
                slider->onDragMove();
            }
        }
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchEnded(touch, event);
        if (auto* slider = getMySlider()) {
            if (slider->m_fields->m_isAffected) {
                slider->onDragEnd();
            }
        }
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchCancelled(touch, event);
        if (auto* slider = getMySlider()) {
            if (slider->m_fields->m_isAffected) {
                slider->onDragEnd();
            }
        }
    }
};
