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

// Temporarily hide/show the popup's blur without destroying it.
// Used by settings popups that hide their chrome while a slider is
// dragged so the list behind acts as a live, unblurred preview.
// Fade is smooth (opacity) and the blur node stays registered.
void setLivePreviewMode(cocos2d::CCNode* popup, bool active, float duration = 0.22f);

} // namespace paimon::popupblur
