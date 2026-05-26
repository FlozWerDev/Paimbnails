#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>

using namespace geode::prelude;

// Smart-enter para ShareCommentLayer. Llama SIEMPRE al original al final
// para no romper la cadena de hooks de otros mods que tambien observen
// enterPressed (ej: traduccion automatica del input, autocorrectores, etc).
class $modify(PaimonShareCommentSmartEnter, ShareCommentLayer) {
    $override
    void enterPressed(CCTextInputNode* node) {
        if (node == m_commentInput && !m_commentInput->getString().empty() && !m_uploadPopup) {
            this->onShare(nullptr);
            return;
        }
        ShareCommentLayer::enterPressed(node);
    }
};
