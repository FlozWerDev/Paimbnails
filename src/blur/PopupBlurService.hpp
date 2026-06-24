#pragma once

#include <Geode/utils/cocos.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::popupblur {

struct Config {
    bool enabled = false;
    std::string style = "paiblur"; // "paiblur" (dynamic) | "paimonblur" | "gaussian"
    float intensity = 4.f;
    float darkness = 0.28f;
};

Config getConfig();

cocos2d::CCTexture2D* captureSceneTexture(
    cocos2d::CCNode* popupToHide,
    cocos2d::CCSize& outSize,
    bool forceRefresh = false,
    int maxCaptureLongEdge = 0
);

cocos2d::CCLayerColor* buildBlurNode(cocos2d::CCSprite* blurred, cocos2d::CCSize const& winSize, Config const& cfg);

cocos2d::CCSprite* normalizeBlurSpriteToWinSize(cocos2d::CCSprite* input, cocos2d::CCSize const& winSize);

bool captureAndApply(cocos2d::CCNode* popup);

bool captureAndApplyWithConfig(cocos2d::CCNode* popup, Config cfg);

void cleanup(cocos2d::CCNode* popup);

void cleanupWithFade(cocos2d::CCNode* popup, float duration);

void registerFlashOverlay(cocos2d::CCNode* flashLayer);
void unregisterFlashOverlay(cocos2d::CCNode* flashLayer);

void cleanupAllActive(float fadeDuration = 0.15f);

void registerExternalBlur(cocos2d::CCNode* popup, cocos2d::CCNode* blurNode);

std::vector<std::pair<cocos2d::CCNode*, bool>> hideAllActiveBlurs();
void restoreHiddenBlurs(std::vector<std::pair<cocos2d::CCNode*, bool>> const& hidden);

cocos2d::CCSprite* reuseBlurForSnapshot(cocos2d::CCTexture2D* snapshot,
                                         std::string const& style,
                                         float intensity,
                                         float darkness);

void storeBlurForSnapshot(cocos2d::CCTexture2D* snapshot,
                           cocos2d::CCSprite* blurredSprite,
                           std::string const& style,
                           float intensity,
                           float darkness);

} // namespace paimon::popupblur