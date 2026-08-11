#include "EditorHelpers.hpp"

#include "../../utils/EditorContext.hpp"

#include <Geode/modify/CCTextInputNode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor {

namespace {
WeakRef<CCTextInputNode> g_focusedInput;
}

void focusCameraOnPoint(LevelEditorLayer* lel, CCPoint objectSpace) {
    if (!lel || !lel->m_objectLayer) return;
    auto* layer = lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    auto world = layer->convertToWorldSpace(objectSpace);
    auto center = win / 2.f;
    layer->setPosition(layer->getPosition() + center - world);
}

void setFocusedTextInput(CCTextInputNode* node) {
    g_focusedInput.swap(node);
}

Ref<CCTextInputNode> focusedTextInput() {
    return g_focusedInput.lock();
}

} // namespace paimon::editor

// Only the editor asks for the focused input, so don't track anywhere else.
class $modify(PaimonFocusedInputNode, CCTextInputNode) {
    $override
    bool onTextFieldAttachWithIME(CCTextFieldTTF* t) {
        auto r = CCTextInputNode::onTextFieldAttachWithIME(t);
        if (paimon::isEditorScene()) paimon::editor::setFocusedTextInput(this);
        return r;
    }

    $override
    bool onTextFieldDetachWithIME(CCTextFieldTTF* t) {
        if (paimon::editor::focusedTextInput() == this) {
            paimon::editor::setFocusedTextInput(nullptr);
        }
        return CCTextInputNode::onTextFieldDetachWithIME(t);
    }
};
