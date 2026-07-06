#pragma once

#include <Geode/utils/cocos.hpp>
#include <string>

namespace paimon::popupblur {

struct Config {
    bool enabled = false;
    std::string style = "paiblur"; // realtime popup blur only
    float intensity = 4.f;
    float darkness = 0.28f;
};

Config getConfig();

bool captureAndApply(cocos2d::CCNode* popup);

bool captureAndApplyWithConfig(cocos2d::CCNode* popup, Config cfg);

void cleanup(cocos2d::CCNode* popup);

void cleanupWithFade(cocos2d::CCNode* popup, float duration);

void cleanupAllActive(float fadeDuration = 0.15f);

void registerExternalBlur(cocos2d::CCNode* popup, cocos2d::CCNode* blurNode);

} // namespace paimon::popupblur
