// Raise editor options limits for button rows / buttons per row.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorOptionsLayer.hpp>
#include <Geode/modify/EditorOptionsLayer.hpp>
#include <algorithm>
#include <string>

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonButtonRows, EditorOptionsLayer) {
    $override
    void onButtonRows(CCObject* sender) {
        if (!moduleEnabled("editor-mod-button-rows")) {
            return EditorOptionsLayer::onButtonRows(sender);
        }
        int delta = sender && sender->getTag() ? 1 : -1;
        m_buttonRows = std::clamp(m_buttonRows + delta, 2, 24);
        if (m_buttonRowsLabel) {
            m_buttonRowsLabel->setString(std::to_string(m_buttonRows).c_str());
        }
    }

    $override
    void onButtonsPerRow(CCObject* sender) {
        if (!moduleEnabled("editor-mod-button-rows")) {
            return EditorOptionsLayer::onButtonsPerRow(sender);
        }
        int delta = sender && sender->getTag() ? 1 : -1;
        m_buttonsPerRow = std::clamp(m_buttonsPerRow + delta, 6, 128);
        if (m_buttonsPerRowLabel) {
            m_buttonsPerRowLabel->setString(std::to_string(m_buttonsPerRow).c_str());
        }
    }
};
