// Strip of main level color channels at the bottom of the editor.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <array>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
// Common channel indices (BG, G, Line, Obj, 1P, 2P, LightBG, 3DL, etc.)
constexpr std::array kChannels = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1, 2, 3, 4, 5, 6, 7, 8};
}

class $modify(PaimonLiveColorsUI, EditorUI) {
    struct Fields {
        CCNode* strip = nullptr;
        std::vector<CCLayerColor*> swatches;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void colorsTick(float) {
        if (!moduleEnabled("editor-mod-live-colors") || !m_fields->strip || !m_editorLayer) return;
        auto* mgr = m_editorLayer->m_effectManager;
        if (!mgr) return;

        size_t i = 0;
        for (int ch : kChannels) {
            if (i >= m_fields->swatches.size()) break;
            auto* sw = m_fields->swatches[i++];
            if (!sw) continue;
            // getColorAction / colorForIdx variants
            auto col = mgr->colorForIndex(ch);
            sw->setColor({col.r, col.g, col.b});
            sw->setOpacity(220);
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-live-colors")) return true;

        auto win = CCDirector::get()->getWinSize();
        auto* strip = CCNode::create();
        strip->setID("paimbnails/live-color-strip");
        strip->setContentSize({static_cast<float>(kChannels.size()) * 18.f, 16.f});
        strip->setPosition({win.width / 2.f - strip->getContentSize().width / 2.f, 8.f});
        strip->setAnchorPoint({0.f, 0.f});

        float x = 0.f;
        for (size_t i = 0; i < kChannels.size(); ++i) {
            auto* sw = CCLayerColor::create({255, 255, 255, 220}, 16.f, 14.f);
            sw->setPosition({x, 0.f});
            strip->addChild(sw);
            m_fields->swatches.push_back(sw);
            x += 18.f;
        }
        this->addChild(strip, 30);
        if (m_uiItems) m_uiItems->addObject(strip);
        m_fields->strip = strip;
        this->schedule(schedule_selector(PaimonLiveColorsUI::colorsTick), 0.2f);
        return true;
    }
};
