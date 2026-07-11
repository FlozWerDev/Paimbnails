// Editor fixes pack — patterns from Tinker (AddToSectionCrashFix, CenteredObjectButtons,
// ImprovedLinkControls, EditorSliderFix, FixTextObjectBounds).
// Always early-return when module off (suite gate).

#include "../EditorModule.hpp"

#include <Geode/binding/CreateMenuItem.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/TextGameObject.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/TextGameObject.hpp>
#include <cmath>
#include <limits>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool fixesOn() { return moduleEnabled("editor-mod-fixes-pack"); }

bool sanitizePos(float& x, float& y) {
    bool changed = false;
    auto fix = [&](float& v) {
        if (!std::isfinite(v)) {
            v = 0.f;
            changed = true;
        } else if (std::abs(v) > 1.e7f) {
            v = std::copysign(1.e7f, v);
            changed = true;
        }
    };
    fix(x);
    fix(y);
    return changed;
}
}

// --- Tinker AddToSectionCrashFix: sanitize before section insert, restore after ---
class $modify(PaimonSectionFix, GJBaseGameLayer) {
    $override
    void addToSection(GameObject* object) {
        if (!fixesOn() || !object) {
            return GJBaseGameLayer::addToSection(object);
        }
        float sx = object->getPositionX();
        float sy = object->getPositionY();
        if (sanitizePos(sx, sy)) {
            log::warn("Sanitized invalid editor object position before section insertion");
            object->setPosition({sx, sy});
        }
        GJBaseGameLayer::addToSection(object);
    }
};

// --- Slider length: map thumb to camera X / max object X (BE/Tinker idea) ---
class $modify(PaimonSliderFixUI, EditorUI) {
    $override
    void updateSlider() {
        EditorUI::updateSlider();
        if (!fixesOn() || !m_editorLayer || !m_positionSlider) return;
        if (!m_editorLayer->m_objects || m_editorLayer->m_objects->count() == 0) return;

        float maxX = 30.f;
        unsigned n = m_editorLayer->m_objects->count();
        for (unsigned i = 0; i < n; ++i) {
            if (auto* obj = typeinfo_cast<GameObject*>(m_editorLayer->m_objects->objectAtIndex(i))) {
                auto const x = obj->getPositionX();
                if (std::isfinite(x)) maxX = std::max(maxX, x);
            }
        }
        auto* layer = m_editorLayer->m_objectLayer;
        if (!layer) return;
        auto win = CCDirector::get()->getWinSize();
        auto cam = layer->convertToNodeSpace(win / 2.f);
        float t = std::clamp(cam.x / std::max(maxX, 1.f), 0.f, 1.f);
        m_positionSlider->setValue(t);
    }
};

// --- Centered create buttons (Tinker CenteredObjectButtons) ---
class $modify(PaimonCenteredButtonsUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void centerCreateButtons() {
        if (!fixesOn() || !m_createButtonArray) return;
        for (auto* item : CCArrayExt<CreateMenuItem*>(m_createButtonArray)) {
            if (!item) continue;
            auto* node = item->getNormalImage();
            if (!node) continue;
            auto size = item->getContentSize();
            if (size.width <= 0.f || size.height <= 0.f) continue;
            node->setAnchorPoint({0.5f, 0.5f});
            node->setPosition(size / 2.f);
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (fixesOn()) {
            Loader::get()->queueInMainThread([self = Ref(this)] {
                if (self) static_cast<PaimonCenteredButtonsUI*>(self.data())->centerCreateButtons();
            });
        }
        return true;
    }

    $override
    void selectBuildTab(int tab) {
        EditorUI::selectBuildTab(tab);
        if (fixesOn()) centerCreateButtons();
    }
};

// --- Link controls: disable when link mode off ---
class $modify(PaimonLinkControlsUI, EditorUI) {
    void refreshLinkControls() {
        if (!fixesOn()) return;
        auto* linkMenu = this->getChildByID("link-controls-menu");
        if (!linkMenu) linkMenu = this->getChildByID("link-menu");
        if (!linkMenu) return;
        bool linkMode = GameManager::get()->getGameVariable("0097");
        for (auto* child : CCArrayExt<CCNode*>(linkMenu->getChildren())) {
            if (auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
                btn->setEnabled(linkMode);
                btn->setOpacity(linkMode ? 255 : 120);
            }
            if (auto* tog = typeinfo_cast<CCMenuItemToggler*>(child)) {
                tog->setEnabled(true); // toggler itself stays clickable
            }
        }
    }

    $override
    void toggleMode(CCObject* sender) {
        EditorUI::toggleMode(sender);
        refreshLinkControls();
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (fixesOn()) {
            Loader::get()->queueInMainThread([self = Ref(this)] {
                if (self) static_cast<PaimonLinkControlsUI*>(self.data())->refreshLinkControls();
            });
        }
        return true;
    }
};

// --- Text object bounds (Tinker FixTextObjectBounds idea) ---
class $modify(PaimonTextBounds, TextGameObject) {
    void fixBounds() {
        if (!fixesOn()) return;
        // Refresh transform control after text/kerning changes so selection box matches art.
        if (auto* ui = EditorUI::get()) {
            ui->updateTransformControl();
        }
    }

    $override
    void updateTextObject(gd::string text, bool defaultFont) {
        TextGameObject::updateTextObject(text, defaultFont);
        fixBounds();
    }
};

// --- Extra NaN guard on setPosition ---
class $modify(PaimonNanFixObject, GameObject) {
    $override
    void setPosition(CCPoint const& pos) {
        auto safe = pos;
        if (fixesOn() && sanitizePos(safe.x, safe.y)) {
            log::warn("Sanitized invalid GameObject position");
            GameObject::setPosition(safe);
            if (auto* editor = LevelEditorLayer::get(); editor && getParent()) {
                editor->updateObjectSection(this);
            }
            return;
        }
        GameObject::setPosition(pos);
    }
};
