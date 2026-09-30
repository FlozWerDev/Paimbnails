#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;

// smart-enter that preserves other mods' enterpressed observers
class $modify(PaimonShareCommentSmartEnter, ShareCommentLayer) {
    $override
    void enterPressed(CCTextInputNode* node) {
        // original handles observers first, then auto-share
        ShareCommentLayer::enterPressed(node);

        if (node && node == m_commentInput && !m_commentInput->getString().empty() && !m_uploadPopup) {
            this->onShare(nullptr);
        }
    }
};
