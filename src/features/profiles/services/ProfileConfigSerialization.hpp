#pragma once

#include "ProfileThumbs.hpp"
#include "../../../utils/JsonHelper.hpp"
#include <algorithm>
#include <limits>

namespace paimon::profiles {
inline ProfileConfig parseConfig(matjson::Value const& json) {
    ProfileConfig config;
    if (!json.isObject()) return config;
    config.hasConfig = true;
    auto number = [&](char const* key, float fallback, float low, float high) {
        return std::clamp(paimon::json::floatOr(json[key], fallback), low, high);
    };
    auto integer = [&](char const* key, int fallback, int low, int high) {
        return std::clamp(paimon::json::integerOr<int>(json[key], fallback), low, high);
    };
    auto color = [](matjson::Value const& value, cocos2d::ccColor3B fallback) {
        auto channel = [&](char const* key, GLubyte defaultValue) {
            return static_cast<GLubyte>(std::clamp(paimon::json::integerOr<int>(value[key], defaultValue), 0, 255));
        };
        return cocos2d::ccColor3B{channel("r", fallback.r), channel("g", fallback.g), channel("b", fallback.b)};
    };
    config.backgroundType = json["backgroundType"].asString().unwrapOr(config.backgroundType);
    config.blurIntensity = number("blurIntensity", config.blurIntensity, 0.f, 10.f);
    config.darkness = number("darkness", config.darkness, 0.f, 1.f);
    config.useGradient = json["useGradient"].asBool().unwrapOr(config.useGradient);
    config.colorA = color(json["colorA"], config.colorA);
    config.colorB = color(json["colorB"], config.colorB);
    config.separatorColor = color(json["separatorColor"], config.separatorColor);
    config.separatorOpacity = integer("separatorOpacity", config.separatorOpacity, 0, 255);
    config.widthFactor = number("widthFactor", config.widthFactor, 0.30f, 0.95f);
    config.gradientEffect = json["gradientEffect"].asString().unwrapOr(config.gradientEffect);
    config.gradientSpeed = number("gradientSpeed", config.gradientSpeed, 0.1f, 5.f);
    config.useVideoAudio = json["useVideoAudio"].asBool().unwrapOr(config.useVideoAudio);
    config.commentBgType = json["commentBgType"].asString().unwrapOr(config.commentBgType);
    config.commentBgThumbnailId = json["commentBgThumbnailId"].asString().unwrapOr(config.commentBgThumbnailId);
    config.commentBgThumbnailPos = integer("commentBgThumbnailPos", config.commentBgThumbnailPos, 1, (std::numeric_limits<int>::max)());
    config.commentBgBannerMode = json["commentBgBannerMode"].asString().unwrapOr(config.commentBgBannerMode);
    config.commentBgSolidColor = color(json["commentBgSolidColor"], config.commentBgSolidColor);
    config.commentBgSolidOpacity = integer("commentBgSolidOpacity", config.commentBgSolidOpacity, 0, 255);
    config.commentBgBlurType = json["commentBgBlurType"].asString().unwrapOr(config.commentBgBlurType);
    config.commentBgBlur = number("commentBgBlur", config.commentBgBlur, 0.f, 10.f);
    config.commentBgDarkness = number("commentBgDarkness", config.commentBgDarkness, 0.f, 1.f);
    return config;
}

inline matjson::Value serializeConfig(ProfileConfig const& config) {
    auto color = [](cocos2d::ccColor3B value) {
        return matjson::makeObject({{"r", static_cast<int>(value.r)}, {"g", static_cast<int>(value.g)}, {"b", static_cast<int>(value.b)}});
    };
    auto json = matjson::makeObject({
        {"backgroundType", config.backgroundType}, {"blurIntensity", config.blurIntensity},
        {"darkness", config.darkness}, {"useGradient", config.useGradient},
        {"colorA", color(config.colorA)}, {"colorB", color(config.colorB)},
        {"separatorColor", color(config.separatorColor)}, {"separatorOpacity", config.separatorOpacity},
        {"widthFactor", config.widthFactor}, {"gradientEffect", config.gradientEffect},
        {"gradientSpeed", config.gradientSpeed}, {"useVideoAudio", config.useVideoAudio},
        {"commentBgType", config.commentBgType}, {"commentBgThumbnailId", config.commentBgThumbnailId},
        {"commentBgThumbnailPos", config.commentBgThumbnailPos}, {"commentBgBannerMode", config.commentBgBannerMode},
        {"commentBgSolidColor", color(config.commentBgSolidColor)}, {"commentBgSolidOpacity", config.commentBgSolidOpacity},
        {"commentBgBlurType", config.commentBgBlurType}, {"commentBgBlur", config.commentBgBlur},
        {"commentBgDarkness", config.commentBgDarkness}
    });
    return json;
}
}
