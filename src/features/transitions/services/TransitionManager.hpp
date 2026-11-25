#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include "TransitionMedia.hpp"

// customizable transition system

#include "TransitionTypes.hpp"

class TransitionManager {
public:
    static TransitionManager& get();

    void loadConfig();
    void saveConfig();

    TransitionConfig getGlobalConfig() const { return m_globalConfig; }
    void setGlobalConfig(TransitionConfig const& cfg);

    TransitionConfig getLevelEntryConfig() const;
    void setLevelEntryConfig(TransitionConfig const& cfg);
    bool hasLevelEntryConfig() const { return m_hasLevelEntryConfig; }
    void clearLevelEntryConfig();

    cocos2d::CCScene* createTransition(
        TransitionConfig const& cfg,
        cocos2d::CCScene* dest,
        bool isPush = false);

    void replaceScene(cocos2d::CCScene* dest);
    void pushScene(cocos2d::CCScene* dest);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v) { m_enabled = v; }

    // custom (Studio) owns scene changes while it is the active transition:
    // dynamic Transition stands down (its saved setting is left untouched).
    // isCustomActiveFor picks the level-entry config for PlayLayer destinations.
    bool isCustomActive() const;
    bool isCustomActiveFor(cocos2d::CCScene* dest) const;

    void tripCustomSafeMode(std::string const& reason);
    bool isCustomSafeModeTripped() const { return m_customSafeModeTripped; }

    static TransitionType typeFromString(std::string const& s);
    static std::string typeToString(TransitionType t);
    static std::string typeDisplayName(TransitionType t);
    static std::string typeDescription(TransitionType t);
    static CommandAction actionFromString(std::string const& s);
    static std::string actionToString(CommandAction a);
    static TransitionEasing easingFromString(std::string const& s);
    static std::string easingToString(TransitionEasing e);
    static std::string easingDisplayName(TransitionEasing e);
    static std::vector<TransitionEasing> const& allEasings();
    static float easeValue(TransitionEasing e, float t);
    static bool isValidTarget(std::string const& target);
    // studio share tab: same JSON shape as transitions.json "commands".
    static std::string commandsToJson(std::vector<TransitionCommand> const& commands);
    static bool commandsFromJson(std::string const& text, std::vector<TransitionCommand>& out);
    static int sanitizeCommand(TransitionCommand& cmd);
    static int sanitizeCommands(std::vector<TransitionCommand>& commands);
    static int sanitizeConfig(TransitionConfig& cfg);

    static std::vector<TransitionType> const& allTypes();

    cocos2d::CCTransitionScene* createNativeTransition(TransitionConfig const& cfg, cocos2d::CCScene* dest) const;

    // studio-timeline scene shared by the Custom and creative-type branches:
    // sanitizes the timeline and creates the scene, falling back to a fade
    // when there is no running scene, no dest, or an empty/invalid timeline.
    // `custom` applies the script cut/min-duration args; `logFallback` logs
    // the Custom branch's fallback line.
    cocos2d::CCScene* createStudioScene(std::vector<TransitionCommand> commands,
                                        cocos2d::CCScene* dest, bool isPush,
                                        TransitionConfig const& cfg, bool custom,
                                        bool logFallback) const;

    std::vector<TransitionCommand> buildPreviewCommands(TransitionType type, float dur) const;

private:
    TransitionManager();
    void navigateScene(cocos2d::CCScene* dest, bool isPush);
    void warmMedia();
    unsigned m_mediaGeneration = 0;
    std::vector<std::shared_ptr<paimon::transitions::TransitionMedia>> m_preloadedMedia;

    std::vector<TransitionCommand> parseScriptFile(std::string const& path) const;
    std::filesystem::path getConfigPath() const;

    TransitionConfig m_globalConfig;
    TransitionConfig m_levelEntryConfig;
    bool m_hasLevelEntryConfig = false;
    bool m_enabled = true;
    bool m_loaded = false;
    bool m_customSafeModeTripped = false;
};
