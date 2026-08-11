#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;

// Smart-enter while preserving other mods' enterPressed observers.
class $modify(PaimonShareCommentSmartEnter, ShareCommentLayer) {
    bool init(gd::string title, int charLimit, CommentType type, int ID, gd::string desc) {
        if (!ShareCommentLayer::init(title, charLimit, type, ID, desc)) return false;
        return true;
    }

    $override
    void enterPressed(CCTextInputNode* node) {
    // Let the original handle observers first, then open auto-share if needed.
        ShareCommentLayer::enterPressed(node);

        if (node == m_commentInput
            && m_commentInput
            && !m_commentInput->getString().empty()
            && !m_uploadPopup) {
            this->onShare(nullptr);
        }
    }
};
