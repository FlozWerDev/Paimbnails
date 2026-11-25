#include "DynamicTransitionManager.hpp"
#include "DynamicPanelTransitions.hpp"
#include "TransitionManager.hpp"
#include "../ui/DynamicTransitionScene.hpp"
#include "LevelEntryEffects.hpp"
#include "NativeSceneNavigation.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/SceneIdentity.hpp"
#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {

struct ButtonOrigin {
    paimon::SceneIdentity scene;
    Rect rect;
    std::chrono::steady_clock::time_point time;
    bool back = false;
};

struct Route {
    std::string from, to;
    Rect origin;
};

ButtonOrigin s_button;
std::vector<Route> s_routes;
bool s_enabled = true;
paimon::SceneIdentity s_backScene;
unsigned s_backDepth = 0;
uint64_t s_backGeneration = 0;

bool eligible(CCScene* scene) {
    return scene && !typeinfo_cast<CCTransitionScene*>(scene) &&
        !scene->getChildByType<PlayLayer>(0) &&
        !paimon::transitions::requiresNativeSceneNavigation(scene);
}

std::string sceneKey(CCScene* scene) {
    if (auto* children = scene->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (typeinfo_cast<CCLayer*>(child) && !typeinfo_cast<CCLayerColor*>(child) &&
                !typeinfo_cast<FLAlertLayer*>(child)) return typeid(*child).name();
        }
    }
    return typeid(*scene).name();
}

bool isBackButton(CCMenuItem* button) {
    auto const id = button->getID().view();
    if (id.find("back") != std::string_view::npos || id.find("return") != std::string_view::npos)
        return true;
    auto* item = typeinfo_cast<CCMenuItemSprite*>(button);
    auto* sprite = item ? typeinfo_cast<CCSprite*>(item->getNormalImage()) : nullptr;
    auto* frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GJ_arrow_01_001.png");
    return sprite && frame && sprite->getTexture() == frame->getTexture() &&
        sprite->getTextureRect().equals(frame->getRect());
}

template<class Enum>
Enum readEnum(char const* key, Enum fallback, int count) {
    auto value = Mod::get()->getSavedValue<int>(key, static_cast<int>(fallback));
    return value >= 0 && value < count ? static_cast<Enum>(value) : fallback;
}

// single source for the saved-key <-> member mapping of the scalar fields.
// enums stay explicit below: each has its own type and value count.
struct BoolField { char const* key; bool Config::* member; };
constexpr BoolField kBoolFields[] = {
    {"dynamic-transition-back", &Config::animateBack},
    {"dynamic-transition-keyboard-back", &Config::animateKeyboardBack},
    {"dynamic-transition-popups", &Config::animatePopups},
    {"dynamic-transition-dropdowns", &Config::animateDropdowns},
    {"dynamic-transition-blocking-layers", &Config::animateBlockingLayers},
    {"dynamic-transition-dialogs", &Config::animateDialogs},
    {"dynamic-transition-editor-panels", &Config::animateEditorPanels},
    {"dynamic-transition-gameplay-panels", &Config::animateGameplayPanels},
    {"dynamic-transition-other-mods", &Config::animateOtherMods},
    {"dynamic-transition-instant", &Config::includeInstant},
    {"dynamic-transition-only-buttons", &Config::onlyFromButton},
    {"dynamic-transition-sync-smooth-ui", &Config::syncSmoothUI},
};

struct FloatField { char const* key; float Config::* member; };
constexpr FloatField kFloatFields[] = {
    {"dynamic-transition-duration", &Config::duration},
    {"dynamic-transition-back-duration", &Config::backDuration},
    {"dynamic-transition-corners", &Config::cornerRadius},
    {"dynamic-transition-background-scale", &Config::backgroundScale},
    {"dynamic-transition-dim", &Config::dim},
    {"dynamic-transition-spring", &Config::spring},
    {"dynamic-transition-button-blend", &Config::buttonBlend},
    {"dynamic-transition-shadow", &Config::shadow},
    {"dynamic-transition-quality", &Config::quality},
};

}

Config getConfig() {
    Config config;
    auto* mod = Mod::get();
    config.enabled = mod->getSettingValue<bool>(kEnabledSetting);
    for (auto const& field : kBoolFields)
        config.*field.member = mod->getSavedValue<bool>(field.key, config.*field.member);
    config.style = readEnum("dynamic-transition-style", config.style, kStyleCount);
    config.curve = readEnum("dynamic-transition-curve", config.curve, kCurveCount);
    config.origin = readEnum("dynamic-transition-origin", config.origin, kOriginCount);
    config.reducedMotion = readEnum("dynamic-transition-reduced-motion", config.reducedMotion, 2);
    config.panelStyle = readEnum("dynamic-transition-panel-style", config.panelStyle, kPanelStyleCount);
    for (auto const& field : kFloatFields)
        config.*field.member = static_cast<float>(mod->getSavedValue<double>(field.key, config.*field.member));
    return sanitize(config);
}

Config animationConfig(Config config) {
    config = sanitize(config);
    if (config.syncSmoothUI && paimon::settings::smoothui::enabled()) {
        float speed = limit(static_cast<float>(paimon::settings::smoothui::globalSpeed()), 1.f, .35f, 2.5f);
        config.duration /= speed;
        config.backDuration /= speed;
        float strength = limit(static_cast<float>(paimon::settings::smoothui::motionStrength()), 1.f, 0.f, 2.f);
        config.spring *= strength;
        config.backgroundScale = 1.f - (1.f - config.backgroundScale) * strength;
    }
    return sanitize(config);
}

void saveConfig(Config config) {
    config = sanitize(config);
    auto* mod = Mod::get();
    s_enabled = config.enabled;
    mod->setSettingValue<bool>(kEnabledSetting, config.enabled);
    for (auto const& field : kBoolFields)
        mod->setSavedValue(field.key, config.*field.member);
    mod->setSavedValue("dynamic-transition-style", static_cast<int>(config.style));
    mod->setSavedValue("dynamic-transition-curve", static_cast<int>(config.curve));
    mod->setSavedValue("dynamic-transition-origin", static_cast<int>(config.origin));
    mod->setSavedValue("dynamic-transition-reduced-motion", static_cast<int>(config.reducedMotion));
    mod->setSavedValue("dynamic-transition-panel-style", static_cast<int>(config.panelStyle));
    for (auto const& field : kFloatFields)
        mod->setSavedValue(field.key, static_cast<double>(config.*field.member));
    if (!config.enabled) clearHistory();
}

void resetConfig() {
    saveConfig(Config{});
    clearHistory();
}

bool isEnabled() {
    return s_enabled;
}

Rect buttonRect(CCNode* button) {
    if (!button) return {};
    auto size = button->getContentSize();
    std::array<CCPoint, 4> points{{button->convertToWorldSpace({0.f, 0.f}),
        button->convertToWorldSpace({size.width, 0.f}),
        button->convertToWorldSpace({size.width, size.height}),
        button->convertToWorldSpace({0.f, size.height})}};
    float left = points[0].x, right = left, bottom = points[0].y, top = bottom;
    for (auto point : points) {
        left = std::min(left, point.x); right = std::max(right, point.x);
        bottom = std::min(bottom, point.y); top = std::max(top, point.y);
    }
    return {left, bottom, right - left, top - bottom};
}

void captureButton(CCMenuItem* button) {
    discardButton();
    if (!s_backDepth) { s_backScene = {}; ++s_backGeneration; }
    auto* scene = CCDirector::get()->getRunningScene();
    if (!isEnabled() || !button || !button->isEnabled() || !scene ||
        typeinfo_cast<CCTransitionScene*>(scene)) return;
    auto* root = static_cast<CCNode*>(button);
    while (root->getParent()) root = root->getParent();
    if (root != scene) return;
    auto rect = buttonRect(button);
    if (rect.width <= 0.f || rect.height <= 0.f) return;
    s_button = {paimon::SceneIdentity(scene), rect, std::chrono::steady_clock::now(), isBackButton(button)};
}

void discardButton() {
    s_button = {};
}

void deferDiscardButton() {
    auto capturedTime = s_button.time;
    Loader::get()->queueInMainThread([capturedTime] {
        if (s_button.time == capturedTime) discardButton();
    });
}

bool currentButton(CCScene* scene, Rect& rect, bool& back) {
    if (!s_button.scene.matches(scene) ||
        std::chrono::steady_clock::now() - s_button.time >= std::chrono::milliseconds(1200)) return false;
    rect = s_button.rect;
    back = s_button.back;
    return true;
}

void beginBackInput() {
    if (s_backDepth++ != 0) return;
    ++s_backGeneration;
    s_backScene = paimon::SceneIdentity(CCDirector::get()->getRunningScene());
    discardButton();
    finishPanelAnimation();
}

void endBackInput() {
    if (!s_backDepth || --s_backDepth) return;
    auto generation = s_backGeneration;
    // deferred navigation runs before this queued expiration.
    Loader::get()->queueInMainThread([generation] {
        if (generation == s_backGeneration && !s_backDepth) s_backScene = {};
    });
}

bool hasBackInput() {
    return s_backScene.matches(CCDirector::get()->getRunningScene());
}

void clearHistory() {
    discardButton();
    s_routes.clear();
    s_backScene = {};
    ++s_backGeneration;
    clearPanelHistory();
}

CCScene* createTransition(CCScene* destination, bool backwards, bool instant) {
    if (paimon::isRuntimeShuttingDown()) return nullptr;
    if (!isEnabled()) { clearHistory(); return nullptr; }
    // custom (Studio) transitions own this scene change: stand down without
    // touching the saved Dynamic setting, so it resumes when Custom turns off.
    if (TransitionManager::get().isCustomActiveFor(destination)) { clearHistory(); return nullptr; }
    Config config = getConfig();
    auto* director = CCDirector::get();
    auto* source = director->getRunningScene();
    if (director->getNextScene()) { discardButton(); return nullptr; }
    if (!eligible(source) || !eligible(destination) || source == destination ||
        paimon::transitions::isLevelExitTransitionPending()) {
        clearHistory();
        return nullptr;
    }

    Rect capturedRect;
    bool backButton = false;
    bool hasButton = currentButton(source, capturedRect, backButton);
    bool keyboardBack = hasBackInput();
    backwards = backwards || backButton || keyboardBack;
    auto size = director->getWinSize();
    if (size.width < 8.f || size.height < 8.f) { clearHistory(); return nullptr; }
    Rect origin = hasButton && config.origin == Origin::Button ? capturedRect :
        fallbackOrigin(size.width, size.height, config.origin);
    auto from = sceneKey(source), to = sceneKey(destination);
    auto route = s_routes.end();
    // recreated layers have new addresses, so return routes use their layer types.
    for (auto it = s_routes.end(); it != s_routes.begin();) {
        --it;
        if (it->from == to && it->to == from && (from != to || backwards || backButton)) {
            route = it;
            backwards = true;
            origin = {it->origin.x * size.width, it->origin.y * size.height,
                it->origin.width * size.width, it->origin.height * size.height};
            break;
        }
    }
    discardButton();
    if ((backwards && !config.animateBack) || (keyboardBack && !config.animateKeyboardBack) ||
        (instant && !config.includeInstant && !backwards) ||
        (config.onlyFromButton && !hasButton && route == s_routes.end())) {
        if (route != s_routes.end()) s_routes.erase(route, s_routes.end());
        return nullptr;
    }

    origin = normalizeOrigin(origin, size.width, size.height);
    if (paimon::settings::smoothui::reducedMotion()) {
        clearHistory();
        if (config.reducedMotion == ReducedMotion::Instant) return destination;
        return DynamicTransitionScene::create(destination, reducedMotionConfig(config), origin, backwards, false);
    }
    config = animationConfig(config);
    finishPanelAnimation();
    auto* transition = DynamicTransitionScene::create(destination, config, origin, backwards,
        config.origin == Origin::Button && (hasButton || route != s_routes.end()));
    if (!transition) return nullptr;
    if (backwards) {
        if (route != s_routes.end()) s_routes.erase(route, s_routes.end());
    } else {
        if (s_routes.size() >= 32) s_routes.erase(s_routes.begin());
        s_routes.push_back({from, to, {origin.x / size.width, origin.y / size.height,
            origin.width / size.width, origin.height / size.height}});
    }
    return transition;
}

}

// panel hooks query this on every addChild, so the setting is mirrored instead of read.
$execute {
    using namespace paimon::transitions::dynamic;
    s_enabled = Mod::get()->getSettingValue<bool>(kEnabledSetting);
    geode::listenForSettingChanges<bool>(kEnabledSetting, [](bool value) { s_enabled = value; });
}
