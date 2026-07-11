// Start position switcher (Q/E) + playtest without start pos button.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/StartPosObject.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <vector>

#include "../../../framework/HookConventions.hpp"
#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {

// Which start pos the switcher forces at playtest.
// -2 = inactive (vanilla behavior), -1 = none/level start, >=0 = list index.
constexpr int kSPInactive = -2;
int g_selectedSP = kSPInactive;

std::vector<StartPosObject*> collectStartPos(LevelEditorLayer* lel) {
    std::vector<StartPosObject*> out;
    if (!lel || !lel->m_objects) return out;
    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (auto* sp = typeinfo_cast<StartPosObject*>(obj)) {
            out.push_back(sp);
        } else if (obj && obj->m_isStartPos) {
            if (auto* sp2 = typeinfo_cast<StartPosObject*>(obj)) out.push_back(sp2);
        }
    }
    std::sort(out.begin(), out.end(), [](StartPosObject* a, StartPosObject* b) {
        return a->getPositionX() < b->getPositionX();
    });
    return out;
}

} // namespace

class $modify(PaimonStartPosUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        int index = -1; // -1 = none / beginning
        std::vector<StartPosObject*> list;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void refreshList() {
        m_fields->list = collectStartPos(m_editorLayer);
        if (m_fields->index >= static_cast<int>(m_fields->list.size())) {
            m_fields->index = static_cast<int>(m_fields->list.size()) - 1;
        }
        updateLabel();
    }

    void updateLabel() {
        if (!m_fields->label) return;
        int n = static_cast<int>(m_fields->list.size());
        if (n == 0) {
            m_fields->label->setString("SP: none");
            return;
        }
        if (m_fields->index < 0) {
            m_fields->label->setString(fmt::format("SP: start / {}", n).c_str());
        } else {
            m_fields->label->setString(
                fmt::format("SP: {} / {}", m_fields->index + 1, n).c_str()
            );
        }
    }

    void applyCurrent() {
        if (!m_editorLayer) return;
        g_selectedSP = m_fields->index;
        if (m_fields->index < 0 || m_fields->list.empty()) {
            m_editorLayer->m_startPosObject = nullptr;
        } else {
            auto* sp = m_fields->list[static_cast<size_t>(m_fields->index)];
            m_editorLayer->m_startPosObject = sp;
            if (sp) focusCameraOnPoint(m_editorLayer, sp->getPosition());
        }
        updateLabel();
    }

    void onPrevSP(CCObject*) {
        if (!moduleEnabled("editor-mod-start-pos")) return;
        refreshList();
        if (m_fields->list.empty()) return;
        if (m_fields->index < 0) m_fields->index = static_cast<int>(m_fields->list.size()) - 1;
        else --m_fields->index;
        applyCurrent();
    }

    void onNextSP(CCObject*) {
        if (!moduleEnabled("editor-mod-start-pos")) return;
        refreshList();
        if (m_fields->list.empty()) return;
        ++m_fields->index;
        if (m_fields->index >= static_cast<int>(m_fields->list.size())) m_fields->index = -1;
        applyCurrent();
    }

    void onNoSPPlay(CCObject*) {
        if (!moduleEnabled("editor-mod-start-pos") || !m_editorLayer) return;
        m_editorLayer->m_startPosObject = nullptr;
        m_fields->index = -1;
        g_selectedSP = -1;
        updateLabel();
        this->onPlaytest(nullptr);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        g_selectedSP = kSPInactive;
        if (!moduleEnabled("editor-mod-start-pos")) return true;

        // One pill: [<]  SP: 1 / 3  [>]  |  [play]  — everything moves together.
        constexpr float kW = 168.f;
        constexpr float kH = 26.f;

        // Centered under the position slider, clear of the right-side edit
        // toolbar (its own CCMenu) so the buttons don't lose touches to it.
        auto const win = CCDirector::get()->getWinSize();
        CCPoint dockCenter{win.width / 2.f, win.height - 60.f};
        if (auto* slider = this->getChildByID("position-slider")) {
            auto const world = slider->getParent()
                ? slider->getParent()->convertToWorldSpace(slider->getPosition())
                : slider->getPosition();
            auto const local = this->convertToNodeSpace(world);
            dockCenter.x = local.x;
            dockCenter.y = local.y - 34.f;
        }
        // Keep the whole pill on screen.
        float const half = kW * 0.5f;
        dockCenter.x = std::clamp(dockCenter.x, half + 4.f, win.width - half - 4.f);

        auto* menu = CCMenu::create();
        menu->setID("paimbnails/start-pos-menu");
        menu->setContentSize({kW, kH});
        menu->setAnchorPoint({0.5f, 0.5f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setPosition(dockCenter);

        if (auto* bg = uikit::hudPill({kW, kH})) {
            bg->setPosition({kW / 2.f, kH / 2.f});
            menu->addChild(bg, -1);
        }

        // Custom: paim_startpos-prev/next/play  |  Fallback: arrows + play
        auto* prevSpr = loadIcon(
            files::startPosPrev, { "GJ_arrow_01_001.png" }, 0.42f
        );
        auto* prev = CCMenuItemSpriteExtra::create(
            prevSpr, this, menu_selector(PaimonStartPosUI::onPrevSP)
        );
        prev->setPosition({16.f, kH / 2.f});
        prev->setID("paimbnails/start-pos-prev");
        menu->addChild(prev);

        auto* lab = CCLabelBMFont::create("SP: start", "bigFont.fnt");
        lab->setScale(0.28f);
        lab->setID("paimbnails/start-pos-label");
        lab->setPosition({70.f, kH / 2.f});
        menu->addChild(lab);
        m_fields->label = lab;

        auto* nextSpr = loadIcon(
            files::startPosNext, { "GJ_arrow_01_001.png" }, 0.42f
        );
        // Only flip vanilla arrow; custom next art should already point right
        if (!hasCustom(files::startPosNext)) {
            nextSpr->setFlipX(true);
        }
        auto* next = CCMenuItemSpriteExtra::create(
            nextSpr, this, menu_selector(PaimonStartPosUI::onNextSP)
        );
        next->setPosition({124.f, kH / 2.f});
        next->setID("paimbnails/start-pos-next");
        menu->addChild(next);

        // Custom play art is ~40pt; vanilla GJ_playBtn2 is much larger, so the
        // fixed 0.28f scale only fits the vanilla frame.
        auto* playSpr = loadIcon(
            files::startPosPlay,
            { "GJ_playBtn2_001.png", "GJ_playBtn_001.png" },
            hasCustom(files::startPosPlay) ? 0.5f : 0.28f
        );
        auto* noSp = CCMenuItemSpriteExtra::create(
            playSpr, this, menu_selector(PaimonStartPosUI::onNoSPPlay)
        );
        noSp->setPosition({151.f, kH / 2.f});
        noSp->setID("paimbnails/start-pos-play-no-sp");
        menu->addChild(noSp);

        this->addChild(menu, 40);
        if (m_uiItems) m_uiItems->addObject(menu);

        refreshList();
        return true;
    }

    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (moduleEnabled("editor-mod-start-pos")
            && paimon::isEditorScene()
            && !focusedTextInput()) {
            auto* kd = CCKeyboardDispatcher::get();
            bool mod = kd && (kd->getControlKeyPressed() || kd->getShiftKeyPressed() || kd->getAltKeyPressed());
            if (!mod) {
                if (key == KEY_Q) {
                    this->onPrevSP(nullptr);
                    return;
                }
                if (key == KEY_E) {
                    this->onNextSP(nullptr);
                    return;
                }
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};

// The playtest spawn is decided in setupLevelStart, which positions the player
// from m_startPosObject. Force the switcher's choice here (and let the original
// handle the players) so playtest starts from the right start pos.
class $modify(PaimonStartPosBase, GJBaseGameLayer) {
    $override
    void setupLevelStart(LevelSettingsObject* settings) {
        auto* lel = typeinfo_cast<LevelEditorLayer*>(this);
        if (lel && g_selectedSP != kSPInactive && moduleEnabled("editor-mod-start-pos")) {
            auto list = collectStartPos(lel);
            if (g_selectedSP >= 0 && !list.empty()) {
                int i = std::min(g_selectedSP, static_cast<int>(list.size()) - 1);
                auto* sp = list[static_cast<size_t>(i)];
                m_startPosObject = sp;
                GJBaseGameLayer::setupLevelStart(sp->m_startSettings);
                return;
            }
            m_startPosObject = nullptr;
            GJBaseGameLayer::setupLevelStart(m_levelSettings);
            return;
        }
        GJBaseGameLayer::setupLevelStart(settings);
    }
};
