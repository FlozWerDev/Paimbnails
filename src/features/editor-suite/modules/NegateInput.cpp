// Press N to negate the currently focused numeric text input.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

bool tryNegate(CCTextInputNode* input) {
    if (!input) return false;
    auto s = std::string(input->getString());
    if (s.empty()) return false;

    // Negation is only valid for a complete finite number. Previously any
    // string containing one digit (for example "Layer 1") was modified.
    char* end = nullptr;
    errno = 0;
    auto const value = std::strtod(s.c_str(), &end);
    if (errno == ERANGE || end == s.c_str() || *end != '\0' || !std::isfinite(value)) {
        return false;
    }

    if (s.front() == '-') {
        input->setString(s.substr(1));
        return true;
    }
    if (s.front() == '+') s.erase(s.begin());
    input->setString(("-" + s).c_str());
    return true;
}

} // namespace

class $modify(PaimonNegateInputNode, CCTextInputNode) {
    $override
    bool onTextFieldAttachWithIME(CCTextFieldTTF* t) {
        auto r = CCTextInputNode::onTextFieldAttachWithIME(t);
        if (moduleEnabled("editor-mod-negate-input") && paimon::isEditorScene()) {
            setFocusedTextInput(this);
        }
        return r;
    }

    $override
    bool onTextFieldDetachWithIME(CCTextFieldTTF* t) {
        if (focusedTextInput() == this) setFocusedTextInput(nullptr);
        return CCTextInputNode::onTextFieldDetachWithIME(t);
    }
};

class $modify(PaimonNegateKeyUI, EditorUI) {
    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        auto input = focusedTextInput();
        if (moduleEnabled("editor-mod-negate-input")
            && key == KEY_N
            && input
            && paimon::isEditorScene()) {
            if (tryNegate(input)) {
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
