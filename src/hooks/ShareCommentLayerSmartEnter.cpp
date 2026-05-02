#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonShareCommentSmartEnter, ShareCommentLayer) {
    $override
    void enterPressed(CCTextInputNode* node) {
        if (node == m_commentInput && !m_commentInput->getString().empty() && !m_uploadPopup) {
            this->onShare(nullptr);
        }
    }
};
