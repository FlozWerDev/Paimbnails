#pragma once

// Gradient rendering core: applies a saved gradient config to any SimplePlayer
// or CCSprite by swapping shader programs, plus the helpers the editor popup
// uses (save/load configs, icon creation, shader cache).

#include "GradientTypes.hpp"
#include "GradientCache.hpp"
#include "../../core/modules/ModuleRegistry.hpp"

namespace paimon::icon_gradients {

inline constexpr char kSavedGradientsKey[] = "icon-gradients-saved-gradients";

// Separate Dual Icons lives inside Paimbnails now: its saved values live on
// this mod and its module toggle is the "is it loaded" check.
inline bool sdiEnabled() {
    return paimon::modules::isEnabled("paimbnails.separatedual.global");
}

template <typename T>
inline T sdiSaved(char const* key, T def) {
    return Mod::get()->getSavedValue<T>(key, def);
}

class GradientUtils {

public:

    static SimplePlayer* createIcon(IconType, bool = false);
    static void fitIcon(SimplePlayer*, CCSize, CCPoint);

    static CCMenuItemToggler* createTypeToggle(bool, CCPoint, CCObject*, SEL_MenuHandler);

    static ccColor3B getPlayerColor(ColorType, bool);

    static int getIconID(IconType, bool = false);
    static bool isGradientSaved(GradientConfig);
    static bool isSettingEnabled(int);

    static std::string getTypeID(IconType);
    static std::string getTypeID(SpriteType);
    static std::string getConfigKey(IconType, bool = false);

    static IconType getIconType(SimplePlayer*);
    static std::vector<GradientConfig> getSavedGradients();

    static GradientConfig configFromObject(const matjson::Value&);
    static GradientConfig getSavedConfig(IconType, ColorType, bool = false);
    static GradientConfig getDefaultConfig(ColorType, bool);
    static matjson::Value getSaveObject(GradientConfig);

    static Gradient getGradient(IconType, bool);

    static void migrateLegacyStorage();
    static void removeSavedGradient(GradientConfig);
    static void saveConfig(GradientConfig, const std::string&, const std::string&);
    static void setIconColors(SimplePlayer*, ColorType, bool, bool = false);

    static void applyGradient(SimplePlayer*, Gradient, bool, bool, int);
    static void applyGradient(SimplePlayer*, GradientConfig, ColorType, bool, bool, int);
    static void applyGradient(CCSprite*, GradientConfig, IconType, ColorType, int, bool, bool, bool, int, bool = false);

    static CCGLProgram* createShader(const std::string&, bool, bool, bool);

    static void patchBatchNode(CCSpriteBatchNode*);
    static void hideSprite(CCSprite*);

};

} // namespace paimon::icon_gradients
