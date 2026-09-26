#pragma once

#include <Geode/utils/cocos.hpp>
#include <string>

namespace paimon::popupblur {

struct Config {
    bool enabled = false;
    float intensity = 4.f;
    float darkness = 0.28f;
};

Config getConfig();

bool captureAndApply(cocos2d::CCNode* popup);

bool captureAndApplyWithConfig(cocos2d::CCNode* popup, Config cfg);

void cleanup(cocos2d::CCNode* popup);

void cleanupWithFade(cocos2d::CCNode* popup, float duration);

void cleanupAllActive(float fadeDuration = 0.15f);

// hide blur while a slider drags so the list behind previews live
void setLivePreviewMode(cocos2d::CCNode* popup, bool active, float duration = 0.22f);

} // namespace paimon::popupblur
