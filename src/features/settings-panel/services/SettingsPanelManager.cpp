#include "SettingsPanelManager.hpp"
#include "../ui/PaimonMultiSettingsPanel.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../blur/PopupBlurService.hpp"

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

    // 1. Obtener la configuracion de blur central
    auto cfg = paimon::popupblur::getConfig();

    CCSprite* blurredBg = nullptr;

    // Solo si el blur esta habilitado capturamos y generamos el sprite
    if (cfg.enabled) {
        CCSize captureSize = CCSizeZero;
        auto* tex = paimon::popupblur::captureSceneTexture(nullptr, captureSize);
        if (tex && captureSize.width > 0.f && captureSize.height > 0.f) {
            float effectiveIntensity = cfg.intensity;
            if (cfg.style == "paimonblur") {
                effectiveIntensity = std::min(10.0f, cfg.intensity * 1.15f + 0.35f);
            }

            // Intentar reusar del cache central para ahorrar GPU
            blurredBg = paimon::popupblur::reuseBlurForSnapshot(tex, cfg.style, effectiveIntensity, cfg.darkness);
            if (!blurredBg) {
                if (cfg.style == "paimonblur") {
                    blurredBg = Shaders::createPopupPaimonBlurredSprite(tex, captureSize, effectiveIntensity);
                } else {
                    blurredBg = Shaders::createPopupBlurredSprite(tex, captureSize, effectiveIntensity);
                }

                if (blurredBg) {
                    paimon::popupblur::storeBlurForSnapshot(tex, blurredBg, cfg.style, effectiveIntensity, cfg.darkness);
                }
            }

            // Normalizar a winSize
            if (blurredBg) {
                if (auto* normalized = paimon::popupblur::normalizeBlurSpriteToWinSize(blurredBg, winSize)) {
                    blurredBg = normalized;
                }
            }
        }
    }

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
