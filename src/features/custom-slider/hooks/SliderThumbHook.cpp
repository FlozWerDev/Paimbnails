#include "SliderThumbHook.hpp"

#include <Geode/modify/Slider.hpp>
#include <Geode/modify/SliderTouchLogic.hpp>

#include "../services/CustomSliderManager.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

class $modify(PaimonSlider, Slider) {
public:
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "Slider::init");
    }

    struct Fields {
        bool m_isAffected = false;
        bool m_savedOriginalImages = false;
        float m_oldValue = 0.f;
        Ref<CCNode> m_originalNormalNode = nullptr;
        Ref<CCNode> m_originalSelectedNode = nullptr;
        Ref<CCNode> m_normalNode = nullptr;
        Ref<CCNode> m_selectedNode = nullptr;
    };

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

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonSlider::applyIconDeferred));
        Slider::onExit();
    }

    void applyIconDeferred(float) {
        if (paimon::isRuntimeShuttingDown()) return;
        refreshThumb();
    }

    void refreshThumb() {
        auto* thumb = this->getThumb();
        if (!thumb) return;

        if (!m_fields->m_savedOriginalImages) {
            m_fields->m_originalNormalNode = thumb->getNormalImage();
            m_fields->m_originalSelectedNode = thumb->getSelectedImage();
            m_fields->m_savedOriginalImages = true;
        }

        auto& mgr = CustomSliderManager::get();
        if (!mgr.config().enabled || !mgr.shouldAffectSlider(this)) {
            restoreSlider(thumb);
            return;
        }

        upgradeSlider(thumb);
        m_fields->m_isAffected = true;
    }

    void restoreSlider(SliderThumb* thumb) {
        if (!m_fields->m_isAffected) return;

        thumb->setNormalImage(m_fields->m_originalNormalNode.data());
        thumb->setSelectedImage(m_fields->m_originalSelectedNode.data());
        m_fields->m_normalNode = nullptr;
        m_fields->m_selectedNode = nullptr;
        m_fields->m_isAffected = false;
    }

    void upgradeSlider(SliderThumb* thumb) {
        auto& mgr = CustomSliderManager::get();
        auto thumbSize = thumb->getContentSize();

        if (m_fields->m_normalNode) m_fields->m_normalNode->stopAllActions();
        if (m_fields->m_selectedNode) m_fields->m_selectedNode->stopAllActions();

        auto makeBase = [&](bool selected) {
            auto* base = CCSprite::create();
            base->setContentSize(thumbSize);
            auto* node = CCSprite::create();
            node->setScale(0.9f);
            base->addChild(node);
            node->setPosition(thumbSize / 2.f);
            mgr.addIconToNode(node, selected);
            setCascadeOpacityDeep(base);
            return std::pair(base, node);
        };
        auto [normalBase, normalNode] = makeBase(false);
        auto [selectedBase, selectedNode] = makeBase(true);

        thumb->setNormalImage(normalBase);
        thumb->setSelectedImage(selectedBase);

        m_fields->m_normalNode = normalNode;
        m_fields->m_selectedNode = selectedNode;
    }

    void onDragBegin() {
        auto& cfg = CustomSliderManager::get().config();
        if (!cfg.animateOnDrag) return;

        auto* node = m_fields->m_selectedNode.data();
        if (!node) return;

        node->stopAllActions();

        if (cfg.animType == SliderAnimType::Bounce || cfg.animType == SliderAnimType::BounceRotate) {
            float targetScale = 0.9f * cfg.animBounceScale;
            node->runAction(CCEaseBackOut::create(
                CCScaleTo::create(cfg.animDuration * 0.5f, targetScale)));
        }

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
            node->setRotation(angle);
        }

        m_fields->m_oldValue = this->getValue();
    }

    void onDragEnd() {
        auto& cfg = CustomSliderManager::get().config();
        if (!cfg.animateOnDrag) return;

        auto* node = m_fields->m_selectedNode.data();
        if (!node) return;

        node->stopAllActions();

        if (cfg.animType == SliderAnimType::Bounce || cfg.animType == SliderAnimType::BounceRotate) {
            node->runAction(CCEaseBackOut::create(
                CCScaleTo::create(cfg.animDuration, 0.9f)));
        }

        if (cfg.animType == SliderAnimType::Rotate || cfg.animType == SliderAnimType::BounceRotate) {
            node->runAction(CCEaseBackOut::create(
                CCRotateTo::create(cfg.animDuration, 0.f)));
        }
    }

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

void paimon::slider::refreshCustomSliders(CCNode* root) {
    if (!root || paimon::isRuntimeShuttingDown()) return;

    std::vector<CCNode*> pending = {root};
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();

        if (auto* slider = typeinfo_cast<Slider*>(node)) {
            static_cast<PaimonSlider*>(slider)->refreshThumb();
            continue;
        }

        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child) pending.push_back(child);
            }
        }
    }
}

class $modify(PaimonSliderTouch, SliderTouchLogic) {
    PaimonSlider* getMySlider() {
        if (!m_slider) return nullptr;
        return static_cast<PaimonSlider*>(m_slider);
    }

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        bool result = SliderTouchLogic::ccTouchBegan(touch, event);
        auto* slider = result ? getMySlider() : nullptr;
        if (slider && slider->m_fields->m_isAffected) slider->onDragBegin();
        return result;
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchMoved(touch, event);
        auto* slider = getMySlider();
        if (slider && slider->m_fields->m_isAffected) slider->onDragMove();
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchEnded(touch, event);
        auto* slider = getMySlider();
        if (slider && slider->m_fields->m_isAffected) slider->onDragEnd();
    }

    // CCMenu::ccTouchCancelled is inline on win: binds mac/ios/android only.
    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchCancelled(touch, event);
        auto* slider = getMySlider();
        if (slider && slider->m_fields->m_isAffected) slider->onDragEnd();
    }
};
