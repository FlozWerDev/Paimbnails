#include "SettingsPanelManager.hpp"
#include "../ui/PaimonMultiSettingsPanel.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"

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
    if (m_panel) {
        showCategory(initialCategory);
        return;
    }

    auto director = CCDirector::sharedDirector();
    auto scene = director->getRunningScene();
    if (!scene) return;

    auto winSize = director->getWinSize();
    int w = static_cast<int>(winSize.width);
    int h = static_cast<int>(winSize.height);
    if (w <= 0 || h <= 0) return;

    // capturar escena actual a textura
    auto rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) return;

    rt->begin();
    scene->visit();
    rt->end();

    auto capturedTex = rt->getSprite()->getTexture();
    if (!capturedTex) return;

    // aplicar blur usando el sistema existente de shaders
    CCSprite* blurredBg = BlurSystem::getInstance()->createBlurredSprite(capturedTex, winSize, 7.0f);

    m_panel = PaimonMultiSettingsPanel::create(blurredBg, initialCategory);
    if (!m_panel) return;

    scene->addChild(m_panel, 10000);
}

void SettingsPanelManager::showCategory(int initialCategory) {
    if (!m_panel) {
        open(initialCategory);
        return;
    }

    m_panel->setSelectedCategory(initialCategory);
}

void SettingsPanelManager::close() {
    if (!m_panel) return;

    // el panel maneja su propia animacion de salida
    m_panel->animateClose();
}
