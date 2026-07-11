// Hold Shift and drag horizontally on a selected duration trigger to change m_duration.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <cmath>

using namespace geode::prelude;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-duration-drag"); }
}

class $modify(PaimonDurationDragUI, EditorUI) {
    struct Fields {
        bool dragging = false;
        float startDur = 0.f;
        float startX = 0.f;
        EffectGameObject* target = nullptr;
    };

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        m_fields->dragging = false;
        m_fields->target = nullptr;
        if (on() && touch) {
            auto* keys = CCKeyboardDispatcher::get();
            if (keys && keys->getShiftKeyPressed()) {
                auto sel = paimon::editor::getSelectedObjects(this);
                for (auto* o : sel) {
                    if (auto* e = typeinfo_cast<EffectGameObject*>(o)) {
                        if (e->m_duration > 0.f || e->m_objectID == 901 || e->m_objectID == 1006
                            || e->m_objectID == 1007 || e->m_objectID == 1346 || e->m_objectID == 2067
                            || e->m_objectID == 1268) {
                            m_fields->dragging = true;
                            m_fields->target = e;
                            m_fields->startDur = std::max(0.f, e->m_duration);
                            m_fields->startX = touch->getLocation().x;
                            return true;
                        }
                    }
                }
            }
        }
        return EditorUI::ccTouchBegan(touch, event);
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (on() && m_fields->dragging && m_fields->target && touch) {
            float dx = touch->getLocation().x - m_fields->startX;
            // 50px ≈ 0.5s
            float next = m_fields->startDur + dx * 0.01f;
            next = std::clamp(next, 0.f, 30.f);
            // Snap 0.05
            next = std::round(next * 20.f) / 20.f;
            m_fields->target->m_duration = next;
            return;
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        if (m_fields->dragging && m_fields->target) {
            Notification::create(
                fmt::format("Duration: {:.2f}s", m_fields->target->m_duration),
                NotificationIcon::Info
            )->show();
            m_fields->dragging = false;
            m_fields->target = nullptr;
            this->updateObjectInfoLabel();
            return;
        }
        EditorUI::ccTouchEnded(touch, event);
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        m_fields->dragging = false;
        m_fields->target = nullptr;
        EditorUI::ccTouchCancelled(touch, event);
    }
};
