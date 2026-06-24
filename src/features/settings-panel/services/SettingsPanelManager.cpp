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

    auto director = CCDirector::get();
    auto scene = director->getRunningScene();
    if (!scene) return;

    auto winSize = director->getWinSize();
    auto cfg = paimon::popupblur::getConfig();

    CCSprite* blurredBg = nullptr;
    if (cfg.enabled) {
        CCSize captureSize = CCSizeZero;
        auto* tex = paimon::popupblur::captureSceneTexture(nullptr, captureSize);
        if (tex && captureSize.width > 0.f && captureSize.height > 0.f) {
            float effectiveIntensity = cfg.intensity;
            if (cfg.style == "paimonblur") {
                effectiveIntensity = std::min(10.0f, cfg.intensity * 1.15f + 0.35f);
            }

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

            // PERF: skip normalizeBlurSpriteToWinSize — createPopupPaimonBlurredSprite /
            // createPopupBlurredSprite already produce a sprite with the correct contentSize
            // and flipY, and PaimonMultiSettingsPanel rescales it via setScaleX/Y to winSize.
            // The extra winSize FBO + visit pass (~1ms at 1080p, more at 4K) was pure overhead.
        }
    }

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