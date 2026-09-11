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

// Every shader key the animation sprites (robot/spider parts, extra sprite,
// line overlays) can ask for, compiled ahead of time on load so mid-gameplay
// frame switches never hitch.
constexpr static std::array cacheIDs = std::to_array<std::string_view>({
    "{}-5-101-false-{}-false-true-55","{}-5-102-false-{}-false-true-55","{}-5-103-false-{}-false-true-55","{}-5-104-false-{}-false-true-55","{}-5-105-false-{}-false-true-55","{}-5-106-false-{}-false-true-55","{}-5-107-false-{}-false-true-55","{}-5-201-false-{}-false-true-55",
    "{}-5-202-false-{}-false-true-55","{}-5-203-false-{}-false-true-55","{}-5-204-false-{}-false-true-55","{}-5-205-false-{}-false-true-55","{}-5-206-false-{}-false-true-55","{}-5-207-false-{}-false-true-55","{}-5-301-false-{}-false-true-55","{}-5-302-false-{}-false-true-55",
    "{}-5-303-false-{}-false-true-55","{}-5-304-false-{}-false-true-55","{}-5-305-false-{}-false-true-55","{}-5-306-false-{}-false-true-55","{}-5-307-false-{}-false-true-55","{}-5-400-false-{}-false-true-55","{}-5-501-false-{}-false-true-55","{}-5-502-false-{}-false-true-55",
    "{}-5-503-false-{}-false-true-55","{}-5-504-false-{}-false-true-55","{}-5-505-false-{}-false-true-55","{}-5-506-false-{}-false-true-55","{}-5-507-false-{}-false-true-55","{}-5-601-false-{}-false-true-55","{}-5-602-false-{}-false-true-55","{}-5-603-false-{}-false-true-55",
    "{}-5-604-false-{}-false-true-55","{}-5-605-false-{}-false-true-55","{}-5-606-false-{}-false-true-55","{}-5-607-false-{}-false-true-55","{}-5-700-false-{}-false-true-55","{}-6-101-false-{}-false-true-55","{}-6-102-false-{}-false-true-55","{}-6-103-false-{}-false-true-55",
    "{}-6-104-false-{}-false-true-55","{}-6-105-false-{}-false-true-55","{}-6-106-false-{}-false-true-55","{}-6-201-false-{}-false-true-55","{}-6-202-false-{}-false-true-55","{}-6-203-false-{}-false-true-55","{}-6-204-false-{}-false-true-55","{}-6-205-false-{}-false-true-55",
    "{}-6-206-false-{}-false-true-55","{}-6-301-false-{}-false-true-55","{}-6-302-false-{}-false-true-55","{}-6-303-false-{}-false-true-55","{}-6-304-false-{}-false-true-55","{}-6-305-false-{}-false-true-55","{}-6-306-false-{}-false-true-55","{}-6-400-false-{}-false-true-55",
    "{}-6-501-false-{}-false-true-55","{}-6-502-false-{}-false-true-55","{}-6-503-false-{}-false-true-55","{}-6-504-false-{}-false-true-55","{}-6-505-false-{}-false-true-55","{}-6-506-false-{}-false-true-55","{}-6-601-false-{}-false-true-55","{}-6-602-false-{}-false-true-55",
    "{}-6-603-false-{}-false-true-55","{}-6-604-false-{}-false-true-55","{}-6-605-false-{}-false-true-55","{}-6-606-false-{}-false-true-55","{}-6-700-false-{}-false-true-55","{}-0-105-false-{}-false-true-2","{}-0-205-false-{}-false-true-2","{}-0-305-false-{}-false-true-2",
    "{}-0-405-false-{}-false-true-2","{}-0-505-false-{}-false-true-2","{}-0-605-false-{}-false-true-2","{}-0-705-false-{}-false-true-2","{}-5-101-false-{}-true-true-55","{}-5-102-false-{}-true-true-55","{}-5-103-false-{}-true-true-55","{}-5-104-false-{}-true-true-55",
    "{}-5-105-false-{}-true-true-55","{}-5-106-false-{}-true-true-55","{}-5-107-false-{}-true-true-55","{}-5-201-false-{}-true-true-55","{}-5-202-false-{}-true-true-55","{}-5-203-false-{}-true-true-55","{}-5-204-false-{}-true-true-55","{}-5-205-false-{}-true-true-55",
    "{}-5-206-false-{}-true-true-55","{}-5-207-false-{}-true-true-55","{}-5-301-false-{}-true-true-55","{}-5-302-false-{}-true-true-55","{}-5-303-false-{}-true-true-55","{}-5-304-false-{}-true-true-55","{}-5-305-false-{}-true-true-55","{}-5-306-false-{}-true-true-55",
    "{}-5-307-false-{}-true-true-55","{}-5-400-false-{}-true-true-55","{}-5-501-false-{}-true-true-55","{}-5-502-false-{}-true-true-55","{}-5-503-false-{}-true-true-55","{}-5-504-false-{}-true-true-55","{}-5-505-false-{}-true-true-55","{}-5-506-false-{}-true-true-55",
    "{}-5-507-false-{}-true-true-55","{}-5-601-false-{}-true-true-55","{}-5-602-false-{}-true-true-55","{}-5-603-false-{}-true-true-55","{}-5-604-false-{}-true-true-55","{}-5-605-false-{}-true-true-55","{}-5-606-false-{}-true-true-55","{}-5-607-false-{}-true-true-55",
    "{}-5-700-false-{}-true-true-55","{}-6-101-false-{}-true-true-55","{}-6-102-false-{}-true-true-55","{}-6-103-false-{}-true-true-55","{}-6-104-false-{}-true-true-55","{}-6-105-false-{}-true-true-55","{}-6-106-false-{}-true-true-55","{}-6-201-false-{}-true-true-55",
    "{}-6-202-false-{}-true-true-55","{}-6-203-false-{}-true-true-55","{}-6-204-false-{}-true-true-55","{}-6-205-false-{}-true-true-55","{}-6-206-false-{}-true-true-55","{}-6-301-false-{}-true-true-55","{}-6-302-false-{}-true-true-55","{}-6-303-false-{}-true-true-55",
    "{}-6-304-false-{}-true-true-55","{}-6-305-false-{}-true-true-55","{}-6-306-false-{}-true-true-55","{}-6-400-false-{}-true-true-55","{}-6-501-false-{}-true-true-55","{}-6-502-false-{}-true-true-55","{}-6-503-false-{}-true-true-55","{}-6-504-false-{}-true-true-55",
    "{}-6-505-false-{}-true-true-55","{}-6-506-false-{}-true-true-55","{}-6-601-false-{}-true-true-55","{}-6-602-false-{}-true-true-55","{}-6-603-false-{}-true-true-55","{}-6-604-false-{}-true-true-55","{}-6-605-false-{}-true-true-55","{}-6-606-false-{}-true-true-55",
    "{}-6-700-false-{}-true-true-55","{}-0-105-false-{}-true-true-2","{}-0-205-false-{}-true-true-2","{}-0-305-false-{}-true-true-2","{}-0-405-false-{}-true-true-2","{}-0-505-false-{}-true-true-2","{}-0-605-false-{}-true-true-2","{}-0-705-false-{}-true-true-2",
    "{}-7-105-false-{}-false-true-2","{}-7-205-false-{}-false-true-2","{}-7-305-false-{}-false-true-2","{}-7-405-false-{}-false-true-2","{}-7-505-false-{}-false-true-2","{}-7-605-false-{}-false-true-2","{}-7-705-false-{}-false-true-2","{}-7-105-false-{}-true-true-2",
    "{}-7-205-false-{}-true-true-2","{}-7-305-false-{}-true-true-2","{}-7-405-false-{}-true-true-2","{}-7-505-false-{}-true-true-2","{}-7-605-false-{}-true-true-2","{}-7-705-false-{}-true-true-2","{}-2-105-false-{}-true-true-2","{}-2-205-false-{}-true-true-2",
    "{}-2-305-false-{}-true-true-2","{}-2-405-false-{}-true-true-2","{}-2-505-false-{}-true-true-2","{}-2-605-false-{}-true-true-2","{}-2-705-false-{}-true-true-2","{}-1-104-false-{}-false-true-44","{}-1-204-false-{}-false-true-44","{}-1-304-false-{}-false-true-44",
    "{}-1-404-false-{}-false-true-44","{}-1-504-false-{}-false-true-44","{}-1-604-false-{}-false-true-44","{}-1-704-false-{}-false-true-44","{}-1-105-false-{}-false-true-2","{}-1-205-false-{}-false-true-2","{}-1-305-false-{}-false-true-2","{}-1-405-false-{}-false-true-2",
    "{}-1-505-false-{}-false-true-2","{}-1-605-false-{}-false-true-2","{}-1-705-false-{}-false-true-2","{}-2-105-false-{}-false-true-2","{}-2-205-false-{}-false-true-2","{}-2-305-false-{}-false-true-2","{}-2-405-false-{}-false-true-2","{}-2-505-false-{}-false-true-2",
    "{}-2-605-false-{}-false-true-2","{}-2-705-false-{}-false-true-2","{}-1-104-false-{}-true-true-44","{}-1-204-false-{}-true-true-44","{}-1-304-false-{}-true-true-44","{}-1-404-false-{}-true-true-44","{}-1-504-false-{}-true-true-44","{}-1-604-false-{}-true-true-44",
    "{}-1-704-false-{}-true-true-44","{}-1-105-false-{}-true-true-2","{}-1-205-false-{}-true-true-2","{}-1-305-false-{}-true-true-2","{}-1-405-false-{}-true-true-2","{}-1-505-false-{}-true-true-2","{}-1-605-false-{}-true-true-2","{}-1-705-false-{}-true-true-2",
    "{}-4-105-false-{}-false-true-2","{}-4-205-false-{}-false-true-2","{}-4-305-false-{}-false-true-2","{}-4-405-false-{}-false-true-2","{}-4-505-false-{}-false-true-2","{}-4-605-false-{}-false-true-2","{}-4-705-false-{}-false-true-2","{}-3-104-false-{}-true-true-44",
    "{}-3-204-false-{}-true-true-44","{}-3-304-false-{}-true-true-44","{}-3-105-false-{}-true-true-2","{}-3-205-false-{}-true-true-2","{}-3-305-false-{}-true-true-2","{}-3-405-false-{}-true-true-2","{}-3-505-false-{}-true-true-2","{}-3-605-false-{}-true-true-2",
    "{}-3-705-false-{}-true-true-2","{}-4-105-false-{}-true-true-2","{}-4-205-false-{}-true-true-2","{}-4-305-false-{}-true-true-2","{}-4-405-false-{}-true-true-2","{}-4-505-false-{}-true-true-2","{}-4-605-false-{}-true-true-2","{}-4-705-false-{}-true-true-2",
    "{}-3-104-false-{}-false-true-44","{}-3-204-false-{}-false-true-44","{}-3-304-false-{}-false-true-44","{}-3-404-false-{}-false-true-44","{}-3-504-false-{}-false-true-44","{}-3-604-false-{}-false-true-44","{}-3-704-false-{}-false-true-44","{}-3-105-false-{}-false-true-2",
    "{}-3-205-false-{}-false-true-2","{}-3-305-false-{}-false-true-2","{}-3-405-false-{}-false-true-2","{}-3-505-false-{}-false-true-2","{}-3-605-false-{}-false-true-2","{}-3-705-false-{}-false-true-2",
});

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

$on_mod(Loaded) {

    if (!Mod::get()->getSavedValue<bool>(kSeparate2PMigration, false)) {
        Mod::get()->setSettingValue<bool>(kSettingSeparate2P, true);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    }

    GradientUtils::migrateLegacyStorage();

    GradientCache::setModDisabled(!Mod::get()->getSettingValue<bool>(kSettingEnabled));
    GradientCache::set2PDisabled(Mod::get()->getSettingValue<bool>(kSettingDisable2P));
    GradientCache::set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
    GradientCache::set2PFlip(Mod::get()->getSettingValue<bool>(kSettingFlip2P));
    GradientCache::setMenuGradientsEnabled(Mod::get()->getSettingValue<bool>(kSettingMenu));

    GradientCache::get().m_increaseLineTolerance = Mod::get()->getSettingValue<bool>(kSettingIncreaseTolerance);

    listenForSettingChanges<bool>(kSettingIncreaseTolerance, [](bool value) {
        GradientCache::get().m_increaseLineTolerance = value;
    });

    listenForSettingChanges<bool>(kSettingEnabled, [](bool value) {
        GradientCache::setModDisabled(!value);
        GradientAnimationManager::get().refreshPrograms();
    });

    listenForSettingChanges<bool>(kSettingDisable2P, [](bool value) {
        GradientCache::set2PDisabled(value);
    });

    listenForSettingChanges<bool>(kSettingSeparate2P, [](bool value) {
        GradientCache::set2PSeparate(value);
        Mod::get()->setSavedValue<bool>(kSeparate2PMigration, true);
    });

    listenForSettingChanges<bool>(kSettingFlip2P, [](bool value) {
        GradientCache::set2PFlip(value);
    });

    listenForSettingChanges<bool>(kSettingMenu, [](bool value) {
        GradientCache::setMenuGradientsEnabled(value);
    });
}

namespace {

// Each key maps to its own CCGLProgram on purpose: applyGradient writes
// per-sprite uniforms (uvMin/uvMax, stops, colors) straight into the program,
// so sharing one instance between two sprites would clobber them. That makes
// the full set ~1240 compiles, and doing it inside $on_mod(Loaded) blocked the
// main thread for over a minute on low-end machines. Instead it runs after the
// game is up, spread across frames with a soft time budget so it never freezes
// the menu waiting for it.
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
    steps.reserve((cacheIDs.size() + uiKeys.size()) * 4);

    for (const auto& str : cacheIDs) {
        if (!keyTypeIsActive(str, active)) continue;
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), true, false)]() {
            GradientUtils::createShader(key, true, false, false);
        });
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), false, true)]() {
            GradientUtils::createShader(key, false, false, true);
        });
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), false, false)]() {
            GradientUtils::createShader(key, false, false, false);
        });
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), true, true)]() {
            GradientUtils::createShader(key, true, false, true);
        });
    }

    for (const auto& str : uiKeys) {
        // The editor preview keys are not tied to a saved type, so they all go.
        // linear (blend=false, line=false) and line (blend=false, line=true)
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), true, false)]() {
            GradientUtils::createShader(key, true, false, false);
        });
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), true, true)]() {
            GradientUtils::createShader(key, true, false, true);
        });
        // radial variants share the same key layout with isLinear=false
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), false, false)]() {
            GradientUtils::createShader(key, false, false, false);
        });
        steps.push_back([key = fmt::format(fmt::runtime(fmt::format("{}"_spr, str)), false, true)]() {
            GradientUtils::createShader(key, false, false, true);
        });
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

void GradientCache::setModDisabled(bool disabled) {
    get().m_disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::isModDisabled() {
    return get().m_disabled;
}

void GradientCache::set2PFlip(bool flip) {
    get().m_p2flip = flip;
}

bool GradientCache::is2PFlip() {
    return get().m_p2flip;
}

void GradientCache::set2PSeparate(bool separate) {
    get().m_p2separate = separate && !get().m_disabled && !get().m_p2disabled;
}

bool GradientCache::is2PSeparate() {
    return get().m_p2separate;
}

void GradientCache::set2PDisabled(bool disabled) {
    get().m_p2disabled = disabled;
    set2PSeparate(Mod::get()->getSettingValue<bool>(kSettingSeparate2P));
}

bool GradientCache::is2PDisabled() {
    return get().m_p2disabled;
}

void GradientCache::setMenuGradientsEnabled(bool enabled) {
    get().m_menuGradients = enabled;
}

bool GradientCache::isMenuGradientsEnabled() {
    return get().m_menuGradients;
}
