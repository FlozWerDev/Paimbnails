#include "SettingsPanelManager.hpp"
#include "../ui/PaimonMultiSettingsPanel.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include <Geode/Geode.hpp>

using namespace cocos2d;

void SettingsPanelManager::toggle(int initialCategory) {
    if (m_panel) {
        close();
        return;
    }

    open(initialCategory);
}

void SettingsPanelManager::open(int initialCategory) {
    if (!paimon::modules::isEnabled("paimbnails.settingspanel.menu")) return;

    if (m_panel) {
        showCategory(initialCategory);
        return;
    }

    auto director = CCDirector::get();
    auto scene = director->getRunningScene();
    if (!scene) return;

    CCSprite* blurredBg = nullptr;

    m_panel = PaimonMultiSettingsPanel::create(blurredBg, initialCategory);
    if (!m_panel) return;

    scene->addChild(m_panel, 10000);
}

void SettingsPanelManager::showCategory(int initialCategory) {
    if (!m_panel) return;
    m_panel->setSelectedCategory(initialCategory);
}

void SettingsPanelManager::close() {
    if (!m_panel) return;
    m_panel->removeFromParent();
    m_panel = nullptr;
}
