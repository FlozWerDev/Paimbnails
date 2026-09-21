#include "GradientCache.hpp"
#include "GradientUtils.hpp"
#include "services/GradientAnimationManager.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/BackgroundPreload.hpp"
#include "../../utils/MainThreadDelay.hpp"

#include <Geode/loader/Event.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

// Every shader key animation sprites can ask for, precompiled on load so mid-gameplay switches never hitch.
std::vector<std::string> buildCacheKeys() {
    std::vector<std::string> keys;
    keys.reserve(246);

    auto add = [&](int type, int id, bool second, int extra) {
        keys.push_back("{}-" + std::to_string(type) + "-" + std::to_string(id)
            + "-false-{}-" + (second ? "true" : "false")
            + "-true-" + std::to_string(extra));
    };

    for (int type : {5, 6}) {
        int units = (type == 5) ? 7 : 6;
        for (int hundred : {1, 2, 3, 5, 6}) {
            for (int unit = 1; unit <= units; ++unit) {
                add(type, hundred * 100 + unit, false, 55);
                add(type, hundred * 100 + unit, true, 55);
            }
        }
        for (bool second : {false, true}) {
            add(type, 400, second, 55);
            add(type, 700, second, 55);
        }
    }

    for (int type : {0, 7, 2, 4}) {
        for (int hundred = 1; hundred <= 7; ++hundred) {
            add(type, hundred * 100 + 5, false, 2);
            add(type, hundred * 100 + 5, true, 2);
        }
    }

    for (int hundred = 1; hundred <= 7; ++hundred) {
        for (bool second : {false, true}) {
            add(1, hundred * 100 + 5, second, 2);
            add(1, hundred * 100 + 4, second, 44);
            add(3, hundred * 100 + 5, second, 2);
        }
        add(3, hundred * 100 + 4, false, 44);
        if (hundred <= 3) add(3, hundred * 100 + 4, true, 44);
    }

    return keys;
}

// The editor popup (preview, buttons, color toggles) and garage use their own
// shader keys that are not in cacheIDs. Compiled with the same staggered
// prewarm so dragging a point or picking a color never hits a synchronous
// on-the-fly GLSL compile (the main source of the stutter).
// Format: {isLinear}-{iconType}-{id}-{blend}-{line}-{secondPlayer}-{playerObject}-{extra}
constexpr static std::array uiKeys = std::to_array<std::string_view>({
    "{}-0-1000-false-false-{}-false-1000",
    "{}-0-105-false-false-{}-false-66",
    "{}-0-205-false-false-{}-false-66",
    "{}-0-305-false-false-{}-false-66",
    "{}-0-405-false-false-{}-false-66",
    "{}-0-505-false-false-{}-false-66",
    "{}-0-605-false-false-{}-false-66",
    "{}-0-705-false-false-{}-false-66",
    "{}-0-105-false-false-{}-false-201",
    "{}-0-205-false-false-{}-false-201",
    "{}-0-305-false-false-{}-false-201",
    "{}-0-405-false-false-{}-false-201",
    "{}-0-505-false-false-{}-false-201",
    "{}-0-605-false-false-{}-false-201",
    "{}-0-705-false-false-{}-false-201",
    "{}-0-105-false-false-{}-false-202",
    "{}-0-205-false-false-{}-false-202",
    "{}-0-305-false-false-{}-false-202",
    "{}-0-405-false-false-{}-false-202",
    "{}-0-505-false-false-{}-false-202",
    "{}-0-605-false-false-{}-false-202",
    "{}-0-705-false-false-{}-false-202",
    "{}-1-105-false-false-{}-false-99",
    "{}-1-205-false-false-{}-false-99",
    "{}-1-305-false-false-{}-false-99",
    "{}-1-405-false-false-{}-false-99",
    "{}-1-505-false-false-{}-false-99",
    "{}-1-605-false-false-{}-false-99",
    "{}-1-705-false-false-{}-false-99",
    "{}-0-105-false-false-{}-false-121",
    "{}-0-205-false-false-{}-false-121",
    "{}-0-305-false-false-{}-false-121",
    "{}-0-405-false-false-{}-false-121",
    "{}-0-505-false-false-{}-false-121",
    "{}-0-605-false-false-{}-false-121",
    "{}-0-705-false-false-{}-false-121",
    "{}-0-105-false-false-{}-false-123",
    "{}-0-205-false-false-{}-false-123",
    "{}-0-305-false-false-{}-false-123",
    "{}-0-405-false-false-{}-false-123",
    "{}-0-505-false-false-{}-false-123",
    "{}-0-605-false-false-{}-false-123",
    "{}-0-705-false-false-{}-false-123",
    "{}-0-105-false-false-{}-false-124",
    "{}-0-205-false-false-{}-false-124",
    "{}-0-305-false-false-{}-false-124",
    "{}-0-405-false-false-{}-false-124",
    "{}-0-505-false-false-{}-false-124",
    "{}-0-605-false-false-{}-false-124",
    "{}-0-705-false-false-{}-false-124",
    "{}-0-105-false-false-{}-false-120",
    "{}-0-205-false-false-{}-false-120",
    "{}-0-305-false-false-{}-false-120",
    "{}-0-405-false-false-{}-false-120",
    "{}-0-505-false-false-{}-false-120",
    "{}-0-605-false-false-{}-false-120",
    "{}-0-705-false-false-{}-false-120",
    "{}-0-105-false-false-{}-false-372",
    "{}-0-205-false-false-{}-false-372",
    "{}-0-305-false-false-{}-false-372",
    "{}-0-405-false-false-{}-false-372",
    "{}-0-505-false-false-{}-false-372",
    "{}-0-605-false-false-{}-false-372",
    "{}-0-705-false-false-{}-false-372",
});

constexpr char const* kSeparate2PMigration = "icon-gradients-separate-2p-default-v2";

using namespace geode::prelude;
using namespace paimon::icon_gradients;

// Plain on/off settings mirrored straight into the cache snapshot.
struct SettingMirror {
    char const* key;
    void (*setter)(bool);
};

constexpr SettingMirror kSettingMirrors[] = {
    {kSettingDisable2P, &GradientCache::set2PDisabled},
    {kSettingFlip2P, &GradientCache::set2PFlip},
    {kSettingMenu, &GradientCache::setMenuGradientsEnabled},
};

$on_mod(Loaded) {

    if (!Mod::get()->getSavedValue<bool>(kSeparate2PMigration, false)) {
        Mod::get()->setSettingValue<bool>(kSettingSeparate2P, true);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    }

    GradientUtils::migrateLegacyStorage();

    auto* mod = Mod::get();

    GradientCache::setModDisabled(!mod->getSettingValue<bool>(kSettingEnabled));
    for (auto& mirror : kSettingMirrors)
        mirror.setter(mod->getSettingValue<bool>(mirror.key));
    GradientCache::set2PSeparate(mod->getSettingValue<bool>(kSettingSeparate2P));
    GradientCache::get().m_increaseLineTolerance = mod->getSettingValue<bool>(kSettingIncreaseTolerance);

    listenForSettingChanges<bool>(kSettingIncreaseTolerance, [](bool value) {
        GradientCache::get().m_increaseLineTolerance = value;
    });

    listenForSettingChanges<bool>(kSettingEnabled, [](bool value) {
        GradientCache::setModDisabled(!value);
        GradientAnimationManager::get().refreshPrograms();
    });

    for (auto& mirror : kSettingMirrors) {
        listenForSettingChanges<bool>(mirror.key, [setter = mirror.setter](bool value) {
            setter(value);
        });
    }

    listenForSettingChanges<bool>(kSettingSeparate2P, [](bool value) {
        GradientCache::set2PSeparate(value);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    });
}

namespace {

// One CCGLProgram per key: uniforms are written per sprite, so sharing would clobber them.
// ~1240 compiles spread across frames on a 2ms budget (blocked the main thread for 1min+ at load).
constexpr auto kPrewarmFrameBudget = std::chrono::milliseconds(2);

void runGradientPrewarm(std::vector<std::function<void()>> steps) {
    if (steps.empty()) return;

    struct State {
        std::vector<std::function<void()>> steps;
        size_t index = 0;
    };

    auto state = std::make_shared<State>(State{std::move(steps), 0});

    auto tick = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakTick = tick;
    *tick = [state, weakTick]() {
        if (paimon::isRuntimeShuttingDown()) return;

        if (GradientCache::isModDisabled()
            || !Mod::get()->getSettingValue<bool>(kSettingPreloadShaders)) return;
        if (!paimon::preload::canRunBackgroundPreload()) {
            if (auto strong = weakTick.lock()) {
                paimon::scheduleMainThreadDelay(0.5f, [strong]() { (*strong)(); });
            }
            return;
        }

        auto deadline = std::chrono::steady_clock::now() + kPrewarmFrameBudget;
        while (state->index < state->steps.size()
               && std::chrono::steady_clock::now() < deadline) {
            state->steps[state->index]();
            ++state->index;
        }

        if (state->index < state->steps.size()) {
            // Strong ref only in the pending continuation; the closure holds a
            // weak self-ref to avoid a self-owning shared_ptr cycle.
            if (auto strong = weakTick.lock()) {
                paimon::scheduleMainThreadDelay(0.f, [strong]() { (*strong)(); });
            }
        }
    };

    (*tick)();
}

// getSavedConfig falls back to a default whose points all share the player
// colour, and applyGradient bails on those before touching a shader. So unless
// the user saved a gradient, none of the keys above is ever looked up and
// compiling them is pure startup cost. When one is saved, cacheIDs are limited
// to the icon types that actually have a config: the second token of a key is
// the IconType, and a saved global config keeps them all. The uiKeys stay
// whole because the editor/garage previews are not tied to a saved type.
std::array<bool, 9> activeGradientTypes() {
    std::array<bool, 9> active{};
    active.fill(false);

    if (Mod::get()->hasSavedValue(GradientUtils::getConfigKey(static_cast<IconType>(-1), false))
        || Mod::get()->hasSavedValue(GradientUtils::getConfigKey(static_cast<IconType>(-1), true))) {
        active.fill(true);
        return active;
    }

    for (int type = 0; type < static_cast<int>(active.size()); ++type) {
        for (bool secondPlayer : {false, true}) {
            if (Mod::get()->hasSavedValue(
                    GradientUtils::getConfigKey(static_cast<IconType>(type), secondPlayer))) {
                active[type] = true;
                break;
            }
        }
    }
    return active;
}

bool hasActiveGradientType(std::array<bool, 9> const& active) {
    return std::ranges::find(active, true) != active.end();
}

bool keyTypeIsActive(std::string_view key, std::array<bool, 9> const& active) {
    auto dash = key.find('-');
    if (dash == std::string_view::npos) return true;

    int type = 0;
    size_t pos = dash + 1;
    while (pos < key.size() && key[pos] >= '0' && key[pos] <= '9') {
        type = type * 10 + (key[pos] - '0');
        ++pos;
    }
    if (type < 0 || type >= static_cast<int>(active.size())) return true;
    return active[type];
}

std::vector<std::function<void()>> buildGradientPrewarmSteps() {
    auto active = activeGradientTypes();
    std::vector<std::function<void()>> steps;
    if (!hasActiveGradientType(active)) return steps;

    static const auto animationKeys = buildCacheKeys();
    steps.reserve((animationKeys.size() + uiKeys.size()) * 4);

    // Each key carries "{}" slots for isLinear/isLine; one compile per corner.
    auto compileCorners = [&steps](std::string const& key) {
        steps.push_back([filled = fmt::format(fmt::runtime(key), true, false)]() {
            GradientUtils::createShader(filled, true, false, false);
        });
        steps.push_back([filled = fmt::format(fmt::runtime(key), false, true)]() {
            GradientUtils::createShader(filled, false, false, true);
        });
        steps.push_back([filled = fmt::format(fmt::runtime(key), false, false)]() {
            GradientUtils::createShader(filled, false, false, false);
        });
        steps.push_back([filled = fmt::format(fmt::runtime(key), true, true)]() {
            GradientUtils::createShader(filled, true, false, true);
        });
    };

    for (auto const& key : animationKeys) {
        if (!keyTypeIsActive(key, active)) continue;
        compileCorners(key);
    }

    for (auto const& key : uiKeys) {
        // The editor preview keys are not tied to a saved type, so they all go.
        compileCorners(std::string(key));
    }

    return steps;
}

} // namespace

void GradientCache::prewarmShaders() {
    if (isModDisabled()) return;
    if (!Mod::get()->getSettingValue<bool>(kSettingPreloadShaders)) return;

    // No saved gradients means no key is ever looked up, so the (empty) step
    // list is the only thing built.
    auto steps = buildGradientPrewarmSteps();
    if (steps.empty()) return;

    GradientUtils::hideSprite(CCSprite::create());
    runGradientPrewarm(std::move(steps));
}


GradientCache& GradientCache::get() {
    static GradientCache instance;
    return instance;
}

void GradientCache::setModDisabled(bool disabled) {
    get().m_disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::isModDisabled() {
    return get().m_disabled;
}

void GradientCache::setMenuGradientsEnabled(bool enabled) {
    get().m_menuGradients = enabled;
}

bool GradientCache::isMenuGradientsEnabled() {
    return get().m_menuGradients;
}

void GradientCache::set2PDisabled(bool disabled) {
    get().m_p2disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::is2PDisabled() {
    return get().m_p2disabled;
}

void GradientCache::set2PSeparate(bool separate) {
    get().m_p2separate = separate && !get().m_disabled && !get().m_p2disabled;
}

bool GradientCache::is2PSeparate() {
    return get().m_p2separate;
}

void GradientCache::set2PFlip(bool flip) {
    get().m_p2flip = flip;
}

bool GradientCache::is2PFlip() {
    return get().m_p2flip;
}

IconType GradientCache::getLastSelected() {
    return get().m_lastSelected;
}

void GradientCache::setLastSelected(IconType type) {
    get().m_lastSelected = type;
}

GradientConfig GradientCache::getCopiedConfig() {
    return get().m_copiedConfig;
}

void GradientCache::setCopiedConfig(GradientConfig config) {
    get().m_copiedConfig = config;
}
