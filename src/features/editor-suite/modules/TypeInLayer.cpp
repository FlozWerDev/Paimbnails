// Type editor layer number, next-free layer, and layer lock.
// Inspired by BetterEdit TypeInZLayer.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../EditorUIKit.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/TextInput.hpp>
#include <set>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonTypeInLayerUI, EditorUI) {
    struct Fields {
        Ref<TextInput> layerInput;
        Ref<CCMenuItemToggler> lockTog;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void applyLayer(int layer) {
        if (!m_editorLayer) return;
        m_editorLayer->m_currentLayer = layer;
        if (m_currentLayerLabel) {
            if (layer < 0) m_currentLayerLabel->setString("All");
            else m_currentLayerLabel->setString(fmt::format("{}", layer).c_str());
        }
        if (m_fields->layerInput) {
            if (layer < 0) m_fields->layerInput->setString("All");
            else m_fields->layerInput->setString(fmt::format("{}", layer));
        }
        this->updateObjectInfoLabel();
        refreshLockVisual();
    }

    bool layerLocked() const {
        if (!m_editorLayer || m_editorLayer->m_currentLayer < 0) return false;
        return m_editorLayer->isLayerLocked(m_editorLayer->m_currentLayer);
    }

    void setLayerLocked(bool locked) {
        if (!m_editorLayer || m_editorLayer->m_currentLayer < 0) return;
        if (m_editorLayer->isLayerLocked(m_editorLayer->m_currentLayer) != locked) {
            m_editorLayer->toggleLockActiveLayer();
        }
    }

    void refreshLockVisual() {
        if (!m_fields->lockTog || !m_editorLayer) return;
        bool all = m_editorLayer->m_currentLayer < 0;
        m_fields->lockTog->setVisible(!all && m_editorLayer->m_layerLockingEnabled);
        if (!all) m_fields->lockTog->toggle(layerLocked());
        if (m_currentLayerLabel) {
            m_currentLayerLabel->setColor(
                !all && layerLocked() ? ccColor3B{255, 170, 60} : ccColor3B{255, 255, 255}
            );
        }
    }

    void onNextFreeLayer(CCObject*) {
        if (!moduleEnabled("editor-mod-type-in-layer") || !m_editorLayer || !m_editorLayer->m_objects) return;
        std::set<short> used;
        for (auto* obj : CCArrayExt<GameObject*>(m_editorLayer->m_objects)) {
            if (!obj) continue;
            used.insert(obj->m_editorLayer);
            used.insert(obj->m_editorLayer2);
        }
        short next = 0;
        for (; next < 10000; ++next) {
            if (!used.contains(next)) break;
        }
        applyLayer(next);
    }

    void onToggleLock(CCObject*) {
        if (!moduleEnabled("editor-mod-type-in-layer") || !m_editorLayer) return;
        if (m_editorLayer->m_currentLayer < 0) return;
        setLayerLocked(!layerLocked());
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-type-in-layer")) return true;

        auto* layerMenu = typeinfo_cast<CCMenu*>(this->getChildByID("layer-menu"));
        if (!layerMenu) return true;

        auto const oldSize = layerMenu->getContentSize();
        layerMenu->setContentSize({140.f, oldSize.height});
        layerMenu->setPositionX(layerMenu->getPositionX() - 12.f);

        auto* input = TextInput::create(40.f, "0");
        input->setScale(0.45f);
        input->setID("paimbnails/layer-input");
        input->setCommonFilter(CommonFilter::Int);
        input->setCallback([this](std::string const& s) {
            if (s == "All" || s == "all" || s == "-1") {
                applyLayer(-1);
                return;
            }
            if (auto v = numFromString<int>(s)) applyLayer(v.unwrap());
        });
        if (m_editorLayer) {
            int cl = m_editorLayer->m_currentLayer;
            input->setString(cl < 0 ? "All" : fmt::format("{}", cl));
        }
        m_fields->layerInput = input;

        auto* nextBtn = uikit::fixedSmallButton("+", "GJ_button_04.png", [this] {
            this->onNextFreeLayer(nullptr);
        });
        if (!nextBtn) return true;
        nextBtn->setID("paimbnails/next-free-layer");

        // Lock toggler
        auto* lockOff = CCSprite::createWithSpriteFrameName("GJ_lock_open_001.png");
        auto* lockOn = CCSprite::createWithSpriteFrameName("GJ_lock_001.png");
        if (!lockOff) lockOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        if (!lockOn) lockOn = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        if (lockOff) lockOff->setScale(0.45f);
        if (lockOn) lockOn->setScale(0.45f);
        auto* lockTog = CCMenuItemToggler::create(
            lockOff, lockOn, this, menu_selector(PaimonTypeInLayerUI::onToggleLock)
        );
        lockTog->setID("paimbnails/layer-lock");
        m_fields->lockTog = lockTog;

        layerMenu->addChild(input);
        layerMenu->addChild(nextBtn);
        layerMenu->addChild(lockTog);
        layerMenu->updateLayout();
        refreshLockVisual();
        return true;
    }
};
