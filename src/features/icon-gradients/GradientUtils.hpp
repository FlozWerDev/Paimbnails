#pragma once

#include "GradientTypes.hpp"
#include "GradientCache.hpp"
#include "../../core/modules/ModuleRegistry.hpp"

#include <array>

namespace paimon::icon_gradients {

inline constexpr char kSavedGradientsKey[] = "icon-gradients-saved-gradients";

// separate dual icons lives inside paimbnails now: its saved values live on
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

    // one-call menu-doll paint: resolves the icon's kind, fetches its
    // gradient and shades it. `extra` selects the shader variant.
    static void paintMenuIcon(SimplePlayer*, bool, int);

    static CCGLProgram* createShader(const std::string&, bool, bool, bool);

    static void enableChildShaders(CCSpriteBatchNode*);
    static void hideSprite(CCSprite*);

};

// shared ui sprites/label for the gradient editor widgets and popups.

// fresh color-button swatch (GJ_colorBtn_001) at a final `scale`.
cocos2d::CCSprite* makeColorSwatch(float scale);

// fresh selection ring (GJ_select_001) at `scale`, hidden until selected.
cocos2d::CCSprite* makeSelectRing(float scale);

// editor label in the shared style, added to `parent` at z-order 2.
cocos2d::CCLabelBMFont* makeGradientLabel(cocos2d::CCNode* parent, char const* text,
                                          cocos2d::CCPoint position, float scale = 0.34f,
                                          char const* font = "bigFont.fnt");

struct PreviewIcon {
    IconType type;
    char const* name;
};

// icon types offered by the animation popups' live preview, in cycle order.
inline constexpr std::array kPreviewIcons = {
    PreviewIcon{IconType::Cube, "Cube"},
    PreviewIcon{IconType::Ship, "Ship"},
    PreviewIcon{IconType::Ball, "Ball"},
    PreviewIcon{IconType::Ufo, "UFO"},
    PreviewIcon{IconType::Wave, "Wave"},
    PreviewIcon{IconType::Robot, "Robot"},
    PreviewIcon{IconType::Spider, "Spider"},
    PreviewIcon{IconType::Swing, "Swing"},
    PreviewIcon{IconType::Jetpack, "Jetpack"},
};

// shader cache slots for each animation popup's live preview; kept distinct
// so the two previews never share uniforms.
inline constexpr int kGradientPreviewShaderTag = 807;
inline constexpr int kCustomPreviewShaderTag = 808;

} // namespace paimon::icon_gradients
