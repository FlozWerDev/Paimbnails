#include "EditorHelpers.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor {

std::vector<GameObject*> getSelectedObjects(EditorUI* ui) {
    std::vector<GameObject*> out;
    if (!ui) return out;

    if (ui->m_selectedObject) {
        out.push_back(ui->m_selectedObject);
    }
    if (ui->m_selectedObjects) {
        for (auto* obj : CCArrayExt<GameObject*>(ui->m_selectedObjects)) {
            if (obj && (out.empty() || obj != out.front())) {
                out.push_back(obj);
            }
        }
    }
    return out;
}

CCPoint selectionCenter(EditorUI* ui) {
    auto objs = getSelectedObjects(ui);
    if (objs.empty()) return {0.f, 0.f};
    float sx = 0.f, sy = 0.f;
    for (auto* o : objs) {
        auto p = o->getPosition();
        sx += p.x;
        sy += p.y;
    }
    auto n = static_cast<float>(objs.size());
    return {sx / n, sy / n};
}

void focusCameraOnPoint(LevelEditorLayer* lel, CCPoint objectSpace) {
    if (!lel || !lel->m_objectLayer) return;
    auto* layer = lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    auto world = layer->convertToWorldSpace(objectSpace);
    auto center = win / 2.f;
    layer->setPosition(layer->getPosition() + center - world);
}

void focusCameraOnSelection(EditorUI* ui) {
    if (!ui || !ui->m_editorLayer) return;
    auto c = selectionCenter(ui);
    if (getSelectedObjects(ui).empty()) return;
    focusCameraOnPoint(ui->m_editorLayer, c);
    // Keep slider / UI in sync
    ui->updateSlider();
}

void moveSelectionToCamera(EditorUI* ui) {
    if (!ui || !ui->m_editorLayer || !ui->m_editorLayer->m_objectLayer) return;
    auto objs = getSelectedObjects(ui);
    if (objs.empty()) return;

    auto* layer = ui->m_editorLayer->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    auto camCenterObj = layer->convertToNodeSpace(win / 2.f);
    auto sel = selectionCenter(ui);
    auto delta = camCenterObj - sel;

    for (auto* o : objs) {
        if (o) ui->moveObject(o, delta);
    }
    ui->updateButtons();
    ui->updateObjectInfoLabel();
}

float clampZoom(float zoom, float minZ, float maxZ) {
    if (!std::isfinite(minZ) || minZ <= 0.f) minZ = 0.1f;
    if (!std::isfinite(maxZ) || maxZ < minZ) maxZ = minZ;
    if (!std::isfinite(zoom)) return 1.f;
    return std::clamp(zoom, minZ, maxZ);
}

namespace {
WeakRef<CCTextInputNode> g_focusedInput;
}

void setFocusedTextInput(CCTextInputNode* node) {
    g_focusedInput.swap(node);
}

Ref<CCTextInputNode> focusedTextInput() {
    return g_focusedInput.lock();
}

} // namespace paimon::editor
