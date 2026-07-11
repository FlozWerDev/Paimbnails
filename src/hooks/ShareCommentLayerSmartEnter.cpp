#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>

using namespace geode::prelude;

// Smart-enter for ShareCommentLayer. Always calls the original last so other
// mods observing enterPressed (input translation, autocorrect, etc.) still fire.
class $modify(PaimonShareCommentSmartEnter, ShareCommentLayer) {
    $override
    void enterPressed(CCTextInputNode* node) {
        // Call the original first so other observers get the event, then check
        // the smart-enter condition for auto-share. Skip if the upload popup was
        // already opened during the original call.
        ShareCommentLayer::enterPressed(node);

        if (node == m_commentInput
            && m_commentInput
            && !m_commentInput->getString().empty()
            && !m_uploadPopup) {
            this->onShare(nullptr);
        }
    }
};
