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

class $modify(PaimonSmartEnterInput, CCTextInputNode) {
    $override
    bool onTextFieldInsertText(cocos2d::CCTextFieldTTF* sender, char const* text, int len, cocos2d::enumKeyCodes key) {
        if (isEnterKey(key, text, len)) {
            if (m_delegate) {
                m_delegate->enterPressed(this);
                m_delegate->textInputReturn(this);
            }
            return true;
        }

        return CCTextInputNode::onTextFieldInsertText(sender, text, len, key);
    }
};
