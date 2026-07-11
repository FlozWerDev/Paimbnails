#include "FakePause.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

using namespace geode::prelude;

namespace paimon::editor {

namespace {

void detachInputAndScheduling(EditorPauseLayer* pause) {
    if (!pause) return;
    pause->setTouchEnabled(false);
    pause->setKeyboardEnabled(false);
    pause->setKeypadEnabled(false);
    if (auto* dispatcher = CCTouchDispatcher::get()) {
        dispatcher->unregisterForcePrio(pause);
        dispatcher->removeDelegate(pause);
    }
    if (auto* dispatcher = CCKeyboardDispatcher::get()) dispatcher->removeDelegate(pause);
    if (auto* director = CCDirector::get()) {
        if (director->m_pKeypadDispatcher) director->m_pKeypadDispatcher->removeDelegate(pause);
        if (director->m_pMouseDispatcher) director->m_pMouseDispatcher->removeDelegate(pause);
        if (director->m_pScheduler) director->m_pScheduler->unscheduleAllForTarget(pause);
        if (director->m_pActionManager) director->m_pActionManager->removeAllActionsFromTarget(pause);
    }
}

} // namespace

bool withFakePause(LevelEditorLayer* lel, std::function<void(EditorPauseLayer*)> const& fn) {
    if (!lel || !fn) return false;

    Ref<EditorPauseLayer> pause = EditorPauseLayer::create(lel);
    if (!pause) return false;
    detachInputAndScheduling(pause.data());
    fn(pause.data());
    return true;
}

bool runBuildHelper(LevelEditorLayer* lel) {
    return withFakePause(lel, [](EditorPauseLayer* p) { p->onBuildHelper(nullptr); });
}

bool runCreateLoop(LevelEditorLayer* lel) {
    return withFakePause(lel, [](EditorPauseLayer* p) { p->onCreateLoop(nullptr); });
}

bool runAlignX(LevelEditorLayer* lel) {
    return withFakePause(lel, [](EditorPauseLayer* p) { p->onAlignX(nullptr); });
}

bool runAlignY(LevelEditorLayer* lel) {
    return withFakePause(lel, [](EditorPauseLayer* p) { p->onAlignY(nullptr); });
}

bool runSaveLevel(LevelEditorLayer* lel) {
    return withFakePause(lel, [](EditorPauseLayer* p) { p->saveLevel(); });
}

bool runSelectAllLeft(LevelEditorLayer* lel) {
    if (!lel || !lel->m_editorUI) return false;
    lel->m_editorUI->selectAllWithDirection(true);
    return true;
}

bool runSelectAllRight(LevelEditorLayer* lel) {
    if (!lel || !lel->m_editorUI) return false;
    lel->m_editorUI->selectAllWithDirection(false);
    return true;
}

} // namespace paimon::editor
