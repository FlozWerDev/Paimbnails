#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace paimon::transitions {

enum class LevelEntryStyle {
    SmoothPlus,
    LegacyBounce,
    Soft,
    Impact,
};

enum class LevelExitMode {
    Disabled,
    MatchEntry,
    Separate,
};

struct LevelEntryEffectsConfig {
    bool enabled = true;
    LevelEntryStyle style = LevelEntryStyle::SmoothPlus;
    float duration = 1.f;
    float intensity = 1.f;
    bool animatePage = true;
    bool animateBackground = true;
    bool animateGround = true;
    bool animateMiddleground = true;
    bool animateHud = true;
    bool animatePlayer = true;
    bool animateObjects = true;
    bool animateGradients = true;
    bool staggerObjects = true;
    bool respectReducedMotion = true;
    LevelExitMode exitMode = LevelExitMode::MatchEntry;
    LevelEntryStyle exitStyle = LevelEntryStyle::Soft;
    float exitDuration = .75f;
    float exitIntensity = 1.f;
};

LevelEntryEffectsConfig getLevelEntryEffectsConfig();
void saveLevelEntryEffectsConfig(LevelEntryEffectsConfig const& config);

std::vector<LevelEntryStyle> const& levelEntryStyles();
std::string levelEntryStyleId(LevelEntryStyle style);
std::string levelEntryStyleName(LevelEntryStyle style);
LevelEntryStyle levelEntryStyleFromId(std::string const& id);

std::vector<LevelExitMode> const& levelExitModes();
std::string levelExitModeId(LevelExitMode mode);
std::string levelExitModeName(LevelExitMode mode);
LevelExitMode levelExitModeFromId(std::string const& id);

bool shouldUseLevelEntryTransition();
cocos2d::CCTransitionScene* createLevelEntryTransition(cocos2d::CCScene* destination);
bool shouldUseLevelExitTransition();
cocos2d::CCTransitionScene* createLevelExitTransition(cocos2d::CCScene* destination);
void beginLevelExitTransition(PlayLayer* playLayer);
void endLevelExitTransition();
bool isLevelExitTransitionPending();

} // namespace paimon::transitions
