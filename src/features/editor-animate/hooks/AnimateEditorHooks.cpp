#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../services/AnimateCompiler.hpp"
#include "../services/AnimateSession.hpp"
#include "../ui/AnimateWidgets.hpp"
#include "../ui/TimelinePanel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

using namespace geode::prelude;
using paimon::animate::AnimateSession;

namespace {

bool enabled() {
    return paimon::modules::isEnabled("paimbnails.animate.editor");
}

AnimateSession& session() {
    return AnimateSession::get();
}

bool attachedTo(LevelEditorLayer* editor) {
    return editor && session().editor() == editor;
}

bool working(EditorUI* ui) {
    return ui && session().isOpen() && attachedTo(ui->m_editorLayer) && !session().isPlaytesting();
}

void toggleTimeline() {
    if (!enabled() || !session().editor()) return;
    session().setOpen(!session().isOpen());
}

bool ready(bool needsOpen) {
    if (!enabled() || !LevelEditorLayer::get() || !session().editor()) return false;
    if (paimon::editor::focusedTextInput() || session().isPlaytesting()) return false;
    return !needsOpen || session().isOpen();
}

void bindKey(char const* key, bool repeatable, bool needsOpen, void (*action)()) {
    KeybindSettingPressedEventV3(Mod::get(), key).listen(
        [repeatable, needsOpen, action](Keybind const&, bool down, bool repeat, double) {
            if (!down || (repeat && !repeatable) || !ready(needsOpen)) return;
            action();
        }
    ).leak();
}

void report(Result<> const& result) {
    if (result.isErr()) paimon::animate::widgets::notifyError(result.unwrapErr());
}

} // namespace

class $modify(PaimonAnimateEditorLayer, LevelEditorLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "LevelEditorLayer::init");
    }

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (noUI || !enabled()) return true;
        session().attach(this);
        if (auto* panel = paimon::animate::TimelinePanel::create(this)) addChild(panel, 9001);
        return true;
    }

    // gd recomputes editor opacity here, so isolation and onion skin go on top of it.
    $override
    void updateVisibility(float dt) {
        LevelEditorLayer::updateVisibility(dt);
        if (attachedTo(this)) session().applyOverrides();
    }

    $override
    void onPlaytest() {
        if (attachedTo(this)) {
            session().stopPlayback();
            paimon::animate::compileStale(this);
            session().releaseOverrides();
        }
        LevelEditorLayer::onPlaytest();
    }

    $override
    void onStopPlaytest() {
        LevelEditorLayer::onStopPlaytest();
        if (attachedTo(this)) session().invalidate();
    }
};

class $modify(PaimonAnimateEditorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!enabled()) return true;

        auto* button = paimon::editor::assets::squareButton(
            "paim_animate.png",
            {"GJ_playEditorBtn_001.png", "GJ_optionsBtn_001.png"},
            0.85f,
            EditorBaseColor::Green,
            [] { toggleTimeline(); },
            paimon::editor::toolbarToggleSize(this)
        );
        if (!button) return true;
        button->setID("animate-button"_spr);

        if (auto* menu = paimon::editor::hostToolbarMenu(this)) {
            menu->addChild(button);
            if (menu->getLayout()) menu->updateLayout();
        } else {
            auto* fallback = CCMenu::create();
            fallback->setID("animate-menu"_spr);
            fallback->setPosition({28.f, CCDirector::get()->getWinSize().height - 200.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }

    $override
    GameObject* createObject(int objectID, CCPoint position) {
        auto* object = EditorUI::createObject(objectID, position);
        if (object && working(this)) session().onObjectCreated(object);
        return object;
    }

    // what you paste lands in the current frame, so it can't be filtered out as locked first.
    $override
    CCArray* pasteObjects(gd::string str, bool withColor, bool noUndo) {
        if (!working(this)) return EditorUI::pasteObjects(str, withColor, noUndo);
        session().suspendLock(true);
        auto* objects = EditorUI::pasteObjects(str, withColor, noUndo);
        session().suspendLock(false);
        session().onObjectsPasted(objects);
        return objects;
    }

    $override
    void selectObject(GameObject* object, bool ignoreFilter) {
        if (working(this) && session().isLocked(object)) return;
        EditorUI::selectObject(object, ignoreFilter);
    }

    $override
    void selectObjects(CCArray* objects, bool ignoreFilter) {
        if (!objects || !working(this)) return EditorUI::selectObjects(objects, ignoreFilter);
        auto* allowed = CCArray::create();
        bool filtered = false;
        for (auto* object : CCArrayExt<GameObject*>(objects)) {
            if (session().isLocked(object)) {
                filtered = true;
                continue;
            }
            allowed->addObject(object);
        }
        EditorUI::selectObjects(filtered ? allowed : objects, ignoreFilter);
    }

    $override
    void scrollWheel(float y, float x) {
        auto* panel = paimon::animate::TimelinePanel::get();
        if (panel && attachedTo(m_editorLayer) && panel->handleScroll(y, x)) return;
        EditorUI::scrollWheel(y, x);
    }
};

class $modify(PaimonAnimatePauseLayer, EditorPauseLayer) {
    // baking before the save lands the triggers in the saved level string.
    $override
    void saveLevel() {
        auto* editor = m_editorLayer;
        bool const attached = attachedTo(editor);
        if (attached) {
            session().stopPlayback();
            paimon::animate::compileStale(editor);
        }
        EditorPauseLayer::saveLevel();
        if (attached) session().persist();
    }
};

$execute {
    bindKey("editor-animate-keybind", false, false, [] { toggleTimeline(); });
    bindKey("editor-animate-play-key", false, true, [] { session().togglePlay(); });
    bindKey("editor-animate-prev-key", true, true, [] {
        session().stopPlayback();
        session().stepFrame(-1);
    });
    bindKey("editor-animate-next-key", true, true, [] {
        session().stopPlayback();
        session().stepFrame(1);
    });
    bindKey("editor-animate-new-frame-key", false, true, [] {
        session().stopPlayback();
        report(session().duplicateFrame());
    });
    bindKey("editor-animate-blank-frame-key", false, true, [] {
        session().stopPlayback();
        report(session().insertFrame(true));
    });
}
