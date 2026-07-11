// Import a reference image into the editor object layer (Tinker-inspired).

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"
#include "../../../utils/FileDialog.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <filesystem>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
bool on() { return moduleEnabled("editor-mod-reference-image"); }

constexpr char const* kNodeId = "paimbnails/reference-image";
constexpr char const* kOpacityKey = "paim-ref-image-opacity";
constexpr char const* kScaleKey = "paim-ref-image-scale";
}

class $modify(PaimonRefImageLEL, LevelEditorLayer) {
    struct Fields {
        Ref<CCSprite> sprite;
        ~Fields() {
            if (sprite) sprite->removeFromParent();
        }
    };
};

class $modify(PaimonRefImageUI, EditorUI) {
    struct Fields {
        Ref<CCMenuItemSpriteExtra> clearButton;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void setClearButtonEnabled(bool enabled) {
        if (!m_fields->clearButton) return;
        m_fields->clearButton->setEnabled(enabled);
        m_fields->clearButton->m_animationEnabled = enabled;
        if (auto* image = typeinfo_cast<CCNodeRGBA*>(
                m_fields->clearButton->getNormalImage()
            )) {
            image->setColor(enabled ? ccColor3B{255, 255, 255} : ccColor3B{150, 150, 150});
            image->setOpacity(enabled ? 255 : 150);
        }
    }

    void clearRef() {
        if (!m_editorLayer || !m_editorLayer->m_objectLayer) return;
        if (auto* n = m_editorLayer->m_objectLayer->getChildByID(kNodeId)) {
            n->removeFromParent();
        }
        auto* self = static_cast<PaimonRefImageLEL*>(m_editorLayer);
        self->m_fields->sprite = nullptr;
        setClearButtonEnabled(false);
    }

    void placeImage(std::filesystem::path const& path) {
        if (!m_editorLayer || !m_editorLayer->m_objectLayer) return;
        clearRef();

        auto* spr = CCSprite::create(path.string().c_str());
        if (!spr) {
            Notification::create("Could not load image", NotificationIcon::Error)->show();
            return;
        }
        spr->setID(kNodeId);
        spr->setOpacity(static_cast<GLubyte>(
            std::clamp(Mod::get()->getSavedValue<int64_t>(kOpacityKey, 120), int64_t{20}, int64_t{255})
        ));
        float sc = static_cast<float>(Mod::get()->getSavedValue<double>(kScaleKey, 1.0));
        if (sc < 0.05f) sc = 0.05f;
        if (sc > 20.f) sc = 20.f;
        spr->setScale(sc);
        spr->setAnchorPoint({0.5f, 0.5f});
        auto const win = CCDirector::get()->getWinSize();
        CCPoint const visibleCenter{
            win.width / 2.f,
            (win.height + std::max(0.f, m_toolbarHeight)) / 2.f
        };
        auto const center = m_editorLayer->m_objectLayer->convertToNodeSpace(visibleCenter);
        spr->setPosition(center);
        m_editorLayer->m_objectLayer->addChild(spr, -50);
        static_cast<PaimonRefImageLEL*>(m_editorLayer)->m_fields->sprite = spr;
        setClearButtonEnabled(true);
        Notification::create("Reference image added", NotificationIcon::Success)->show();
    }

    void onPickRef(CCObject*) {
        if (!on()) return;
        pt::pickImage([self = Ref(this)](Result<std::optional<std::filesystem::path>> res) {
            if (res.isErr() || !self) return;
            auto opt = res.unwrap();
            if (!opt) return;
            static_cast<PaimonRefImageUI*>(self.data())->placeImage(*opt);
        });
    }

    void onClearRef(CCObject*) {
        clearRef();
        Notification::create("Reference image cleared", NotificationIcon::Info)->show();
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;

        if (!m_editButtonBar || !m_editButtonBar->m_buttonArray) return true;

        // Tinker places Reference Image in the EDIT button bar. Keeping import
        // and clear together avoids overcrowding the top undo menu.
        if (auto* btn = editBarToolButton(
                files::refImage,
                { "GJ_downloadBtn_001.png", "GJ_plusBtn_001.png" },
                [this] { this->onPickRef(nullptr); }
            )) {
            btn->setID("paimbnails/ref-image-btn");
            m_editButtonBar->m_buttonArray->addObject(btn);
        }
        if (auto* clr = editBarToolButton(
                files::refClear,
                { "GJ_deleteIcon_001.png", "GJ_trashBtn_001.png" },
                [this] { this->onClearRef(nullptr); }
            )) {
            clr->setID("paimbnails/ref-image-clear");
            m_editButtonBar->m_buttonArray->addObject(clr);
            m_fields->clearButton = clr;
            setClearButtonEnabled(false);
        }

        int cols = GameManager::get()->getIntGameVariable("0049");
        int rows = GameManager::get()->getIntGameVariable("0050");
        if (cols < 1) cols = 6;
        if (rows < 1) rows = 2;
        cols = std::clamp(cols, 1, 12);
        rows = std::clamp(rows, 1, 6);
        m_editButtonBar->reloadItems(cols, rows);
        return true;
    }
};
