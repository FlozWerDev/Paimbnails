#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <cstring>

using namespace geode::prelude;

namespace {
    bool isEnterKey(cocos2d::enumKeyCodes key, char const* text, int len) {
        if (key == cocos2d::KEY_Enter || key == cocos2d::KEY_NumEnter) return true;
        return text && len > 0 && (std::strcmp(text, "\n") == 0 || std::strcmp(text, "\r") == 0);
    }
}

// Hook compat-friendly:
//   1. Siempre delega primero al original. Si el input es multi-linea,
//      el original aceptara el Enter (insertara '\n') y devolvera true:
//      en ese caso NO disparamos enterPressed para no romper esa UI.
//   2. Si el original rechaza el Enter (single-line GD-style, devuelve
//      false), disparamos enterPressed/textInputReturn — comportamiento
//      "smart enter" tradicional de Paimbnails.
//   3. Otras teclas: passthrough total al original.
//
// Esto evita el patron anti-compat de antes (consumiamos Enter en TODOS
// los CCTextInputNode del juego sin llamar al original, rompiendo inputs
// multilinea de otros mods).
class $modify(PaimonSmartEnterInput, CCTextInputNode) {
    static void onModify(auto& self) {
        // Late post: dejamos que otros mods con prioridad estandar reaccionen
        // primero al insert; solo entonces decidimos si activamos smart-enter.
        (void)self.setHookPriorityPost("CCTextInputNode::onTextFieldInsertText", geode::Priority::Late);
    }

    $override
    bool onTextFieldInsertText(cocos2d::CCTextFieldTTF* sender, char const* text, int len, cocos2d::enumKeyCodes key) {
        bool acceptedByOriginal = CCTextInputNode::onTextFieldInsertText(sender, text, len, key);

        if (isEnterKey(key, text, len) && !acceptedByOriginal) {
            if (m_delegate) {
                m_delegate->enterPressed(this);
                m_delegate->textInputReturn(this);
            }
            return true;
        }

        return acceptedByOriginal;
    }
};
