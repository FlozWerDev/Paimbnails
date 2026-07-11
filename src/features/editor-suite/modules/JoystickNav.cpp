// Simple joystick / arrow-key camera pan for the editor (Tinker-inspired, desktop+controller).
// Hold Right Alt + WASD (or arrows) to pan the canvas without moving objects.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-joystick-nav"); }

float panSpeed() {
    return static_cast<float>(moduleSetting<double>("editor-mod-joystick-speed", 12.0));
}
}

class $modify(PaimonJoystickNavUI, EditorUI) {
    struct Fields {
        bool left = false, right = false, up = false, down = false;
        bool active = false;
    };

    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (on()) {
            auto* kd = CCKeyboardDispatcher::get();
            bool panMod = kd && kd->getAltKeyPressed(); // Right/Left Alt
            if (panMod) {
                bool handled = true;
                if (key == KEY_A || key == KEY_Left) m_fields->left = true;
                else if (key == KEY_D || key == KEY_Right) m_fields->right = true;
                else if (key == KEY_W || key == KEY_Up) m_fields->up = true;
                else if (key == KEY_S || key == KEY_Down) m_fields->down = true;
                else handled = false;
                if (handled) {
                    m_fields->active = true;
                    return;
                }
            }
        }
        EditorUI::keyDown(key, timestamp);
    }

    $override
    void keyUp(enumKeyCodes key, double timestamp) {
        if (key == KEY_A || key == KEY_Left) m_fields->left = false;
        else if (key == KEY_D || key == KEY_Right) m_fields->right = false;
        else if (key == KEY_W || key == KEY_Up) m_fields->up = false;
        else if (key == KEY_S || key == KEY_Down) m_fields->down = false;
        if (!m_fields->left && !m_fields->right && !m_fields->up && !m_fields->down) {
            m_fields->active = false;
        }
        EditorUI::keyUp(key, timestamp);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (on()) {
            this->schedule(schedule_selector(PaimonJoystickNavUI::panTick));
        }
        return true;
    }

    void panTick(float dt) {
        if (!on() || !m_fields->active || !m_editorLayer || !m_editorLayer->m_objectLayer) return;
        if (m_editorLayer->m_playbackMode == PlaybackMode::Playing) return;
        float sp = panSpeed() * 60.f * dt;
        float dx = 0.f, dy = 0.f;
        if (m_fields->left) dx += sp;
        if (m_fields->right) dx -= sp;
        if (m_fields->up) dy -= sp;
        if (m_fields->down) dy += sp;
        if (dx == 0.f && dy == 0.f) return;
        auto* layer = m_editorLayer->m_objectLayer;
        layer->setPosition(layer->getPosition() + ccp(dx, dy));
        this->updateSlider();
    }
};
