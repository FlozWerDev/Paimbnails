// Custom grid size controls (+/- presets and free input).
// Top HUD pill:  [zoom-]  30.00  [zoom+]
// Docked near the position slider; X defaults to 570.

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/ObjectToolbox.hpp>
#include <Geode/ui/TextInput.hpp>
#include <array>
#include <cmath>
#include <string>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {

bool gridOn() { return moduleEnabled("editor-mod-grid-control"); }

constexpr std::array kPresets = {3.75f, 7.5f, 15.f, 30.f, 60.f, 90.f, 120.f};

constexpr float kDockX = 450.f;
constexpr float kPillW = 72.f;
constexpr float kPillH = 30.f;
constexpr float kBtnIcon = 18.f;

float loadGridSize() {
    return Mod::get()->getSavedValue<float>("paim-editor-grid-size", 30.f);
}

void saveGridSize(float v) {
    Mod::get()->setSavedValue("paim-editor-grid-size", v);
}

bool isCustomGrid(float size) {
    return size >= 0.9f && std::round(size) != 30.f;
}

} // namespace

class $modify(PaimonGridObjectToolbox, ObjectToolbox) {
    $override
    float gridNodeSizeForKey(int id) {
        if (!gridOn()) return ObjectToolbox::gridNodeSizeForKey(id);
        float size = loadGridSize();
        if (!isCustomGrid(size)) return ObjectToolbox::gridNodeSizeForKey(id);
        return size;
    }
};

class $modify(PaimonGridEditorUI, EditorUI) {
    struct Fields {
        TextInput* input = nullptr;
        CCMenu* container = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void applyGrid(float value, bool updateInput) {
        if (value < 0.9f || value > 120.1f) value = 30.f;
        saveGridSize(value);
        this->updateGridNodeSize();
        if (updateInput && m_fields->input) {
            m_fields->input->setString(fmt::format("{:.2f}", value));
        }
        if (moduleSetting<bool>("editor-mod-grid-show-on-change", true)
            && isCustomGrid(value)) {
            GameManager::get()->setGameVariable("0038", true);
            if (m_editorLayer) m_editorLayer->updateOptions();
        }
    }

    void onGridDec(CCObject*) {
        if (!gridOn()) return;
        float value = loadGridSize();
        auto it = std::lower_bound(kPresets.begin(), kPresets.end(), value - 0.001f);
        if (it != kPresets.begin()) --it;
        applyGrid(*it, true);
    }

    void onGridInc(CCObject*) {
        if (!gridOn()) return;
        float value = loadGridSize();
        auto it = std::upper_bound(kPresets.begin(), kPresets.end(), value + 0.001f);
        if (it == kPresets.end()) --it;
        applyGrid(*it, true);
    }

    $override
    void updateGridNodeSize() {
        if (!gridOn() || !isCustomGrid(loadGridSize())) {
            return EditorUI::updateGridNodeSize();
        }
        // GD only applies custom grid in create mode; temporarily switch.
        auto orig = m_selectedMode;
        m_selectedMode = 2;
        EditorUI::updateGridNodeSize();
        m_selectedMode = orig;
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!gridOn()) return true;

        auto* root = CCMenu::create();
        if (!root) return true;
        root->setContentSize({kPillW, kPillH});
        root->setID("paimbnails/grid-size-controls");
        root->setAnchorPoint({0.5f, 0.5f});
        root->ignoreAnchorPointForPosition(false);

        if (auto* bg = uikit::hudPill({kPillW, kPillH})) {
            bg->setPosition({kPillW / 2.f, kPillH / 2.f});
            root->addChild(bg, -1);
        }

        // Original zoom icons (not ButtonSprite +/- plates).
        auto makeZoomButton = [this, root](char const* frame, float x, char const* id,
                                           SEL_MenuHandler handler) {
            auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
            if (!spr) return;
            auto sz = spr->getContentSize();
            float maxDim = std::max(sz.width, sz.height);
            if (maxDim > 1.f) spr->setScale(kBtnIcon / maxDim);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler);
            btn->setPosition({x, kPillH / 2.f});
            btn->setID(id);
            root->addChild(btn);
        };
        makeZoomButton(
            "GJ_zoomOutBtn_001.png", 12.f, "paimbnails/grid-size-dec",
            menu_selector(PaimonGridEditorUI::onGridDec)
        );

        auto* input = TextInput::create(56.f, "30");
        if (input) {
            input->setCommonFilter(CommonFilter::Float);
            input->setScale(0.5f);
            if (auto* bg = input->getBGSprite()) bg->setVisible(false);
            input->setID("paimbnails/grid-size-input");
            input->setString(fmt::format("{:.2f}", loadGridSize()));
            input->setCallback([this](std::string const& s) {
                auto v = numFromString<float>(s);
                if (v) applyGrid(v.unwrap(), false);
            });
            input->setPosition({36.f, kPillH / 2.f});
            root->addChild(input);
            m_fields->input = input;
        }

        makeZoomButton(
            "GJ_zoomInBtn_001.png", 60.f, "paimbnails/grid-size-inc",
            menu_selector(PaimonGridEditorUI::onGridInc)
        );

        auto const win = CCDirector::get()->getWinSize();
        float dockY = win.height - 20.f;
        if (auto* slider = this->getChildByID("position-slider")) {
            auto const world = slider->getParent()
                ? slider->getParent()->convertToWorldSpace(slider->getPosition())
                : slider->getPosition();
            auto const local = this->convertToNodeSpace(world);
            dockY = local.y;
        }

        // Honor kDockX directly; only clamp so the pill stays on screen.
        float half = kPillW * 0.5f;
        float x = std::clamp(kDockX, half + 4.f, win.width - half - 4.f);

        root->setPosition({x, dockY});
        this->addChild(root, 55);
        if (m_uiItems) m_uiItems->addObject(root);
        m_fields->container = root;

        applyGrid(loadGridSize(), true);
        return true;
    }
};
