#include "ThumbAlerts.hpp"

#include "services/NewThumbWatcher.hpp"
#include "ui/ThumbAlertCard.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../utils/MainThreadDelay.hpp"
#include "../thumbnails/services/LocalThumbs.hpp"
#include "../thumbnails/services/ThumbnailLoader.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/OverlayManager.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <memory>

using namespace geode::prelude;

namespace paimon::thumbalerts {

namespace {

// Above the pause menu and popups, below the capture overlay (999000).
constexpr int kZOrder = 998500;
constexpr float kScreenMargin = 10.f;
constexpr float kThumbWait = 4.f;
constexpr size_t kMaxQueued = 12;

constexpr std::array kSpotNames{
    "top-left", "top-center", "top-right",
    "mid-left", "mid-right",
    "bottom-left", "bottom-center", "bottom-right",
};
constexpr std::array kEnterNames{
    "none", "slide", "fade", "pop", "drop", "flip", "zoom", "elastic", "unfold",
    "swing", "spiral",
};
constexpr std::array kExitNames{
    "none", "slide", "fade", "shrink", "fall", "flip", "zoom", "spin", "fold",
};
constexpr std::array kIdleNames{"none", "float", "pulse", "sway", "tilt"};
constexpr std::array kSoundNames{"none", "soft", "coin", "crystal", "achievement"};

template <size_t N>
int pickIndex(char const* key, std::array<char const*, N> const& names, int fallback) {
    auto const value = Mod::get()->getSettingValue<std::string>(key);
    for (size_t index = 0; index < N; ++index) {
        if (value == names[index]) return static_cast<int>(index);
    }
    return fallback;
}

float settingFloat(char const* key, float fallback, float low, float high) {
    auto const value = static_cast<float>(Mod::get()->getSettingValue<double>(key));
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

int settingInt(char const* key, int low, int high) {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>(key), low, high));
}

char const* soundFile(Sound sound) {
    switch (sound) {
        case Sound::Soft: return "chestClick.ogg";
        case Sound::Coin: return "gold01.ogg";
        case Sound::Crystal: return "crystal01.ogg";
        case Sound::Achievement: return "achievement_01.ogg";
        default: return nullptr;
    }
}

std::deque<NewThumb>& queue() {
    static auto* items = new std::deque<NewThumb>();
    return *items;
}

bool g_showing = false;
// Bumped whenever the queue is reset, so a thumbnail download that lands late
// cannot push a card the user already moved past.
uint64_t g_generation = 0;

void pump();

CCPoint restPoint(Config const& config, CCSize card) {
    auto const win = CCDirector::get()->getWinSize();
    float const scale = std::clamp(config.scale, kMinScale, kMaxScale);
    float const halfW = card.width * scale / 2.f;
    float const halfH = card.height * scale / 2.f;

    float x = win.width / 2.f;
    switch (config.spot) {
        case Spot::TopLeft:
        case Spot::MidLeft:
        case Spot::BottomLeft:
            x = kScreenMargin + halfW;
            break;
        case Spot::TopRight:
        case Spot::MidRight:
        case Spot::BottomRight:
            x = win.width - kScreenMargin - halfW;
            break;
        default: break;
    }

    float y = win.height / 2.f;
    switch (config.spot) {
        case Spot::TopLeft:
        case Spot::TopCenter:
        case Spot::TopRight:
            y = win.height - kScreenMargin - halfH;
            break;
        case Spot::BottomLeft:
        case Spot::BottomCenter:
        case Spot::BottomRight:
            y = kScreenMargin + halfH;
            break;
        default: break;
    }

    x = std::clamp(x + config.offsetX, halfW, std::max(halfW, win.width - halfW));
    y = std::clamp(y + config.offsetY, halfH, std::max(halfH, win.height - halfH));
    return {x, y};
}

void finishCurrent(Config const& config, uint64_t generation) {
    if (generation != g_generation) return;
    g_showing = false;
    if (queue().empty()) return;
    paimon::scheduleMainThreadDelay(std::clamp(config.gap, 0.f, 5.f), [] { pump(); });
}

void present(NewThumb const& item, CCTexture2D* thumbnail, Config const& config,
             uint64_t generation) {
    auto* overlay = OverlayManager::get();
    auto* card = overlay ? ThumbAlertCard::create(item, config, thumbnail) : nullptr;
    if (!card) {
        finishCurrent(config, generation);
        return;
    }

    card->placeAt(restPoint(config, card->getContentSize()), config.scale);
    card->setOnFinished([config, generation] { finishCurrent(config, generation); });
    overlay->addChild(card, kZOrder);

    // A card torn down without running its exit (scene wipe, GL reload) would
    // never release the queue, and no alert would come out again this session.
    float const lifetime = config.enterTime + config.hold + config.exitTime + 5.f;
    paimon::scheduleMainThreadDelay(lifetime, [config, generation] {
        if (generation != g_generation || !g_showing) return;
        finishCurrent(config, generation);
    });

    if (char const* file = soundFile(config.sound)) {
        if (auto* engine = FMODAudioEngine::sharedEngine()) engine->playEffect(file);
    }
}

void pump() {
    if (g_showing || queue().empty() || paimon::isRuntimeShuttingDown()) return;

    auto const item = queue().front();
    queue().pop_front();
    g_showing = true;

    auto const config = readConfig();
    auto const generation = ++g_generation;

    if (item.levelId <= 0) {
        present(item, nullptr, config, generation);
        return;
    }
    if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(item.levelId)) {
        present(item, cached, config, generation);
        return;
    }
    if (auto* local = LocalThumbs::get().loadTexture(item.levelId)) {
        present(item, local, config, generation);
        return;
    }

    // Whichever comes first wins: the download, or the patience cut-off. The
    // card is still worth showing over a plain plate.
    auto shown = std::make_shared<bool>(false);
    auto reveal = [item, config, generation, shown](CCTexture2D* texture) {
        if (*shown || generation != g_generation || paimon::isRuntimeShuttingDown()) return;
        *shown = true;
        present(item, texture, config, generation);
    };

    ThumbnailLoader::get().requestLoad(
        item.levelId, fmt::format("{}.png", item.levelId),
        [reveal](CCTexture2D* texture, bool ok) {
            Ref<CCTexture2D> held = ok ? texture : nullptr;
            Loader::get()->queueInMainThread([reveal, held] { reveal(held.data()); });
        },
        ThumbnailLoader::PriorityHero);

    paimon::scheduleMainThreadDelay(kThumbWait, [reveal] { reveal(nullptr); });
}

void enqueue(NewThumb item, bool ignoreSceneFilter) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!paimon::modules::isEnabled(kModuleId)) return;

    auto const config = readConfig();
    if (!config.enabled) return;
    if (!ignoreSceneFilter && !alertsAllowedHere(config)) return;
    if (queue().size() >= kMaxQueued) return;

    queue().push_back(std::move(item));
    pump();
}

// The same star/demon mapping the server applies in normalizeLevelMeta, so the
// uploader's card reads exactly like the one everybody else will get.
std::string difficultyName(bool autoLevel, bool demon, int demonDifficulty, int stars) {
    if (autoLevel) return "Auto";
    if (demon) {
        switch (demonDifficulty) {
            case 3: return "Easy Demon";
            case 4: return "Medium Demon";
            case 5: return "Insane Demon";
            case 6: return "Extreme Demon";
            default: return "Hard Demon";
        }
    }
    if (stars >= 10) return "Demon";
    if (stars >= 8) return "Insane";
    if (stars >= 6) return "Harder";
    if (stars >= 4) return "Hard";
    if (stars == 3) return "Normal";
    if (stars == 2) return "Easy";
    if (stars == 1) return "Auto";
    return "Unrated";
}

NewThumb thumbFromLevelMeta(int levelId, std::string const& levelMeta) {
    NewThumb item;
    item.levelId = levelId;

    auto parsed = matjson::parse(levelMeta);
    if (!parsed.isOk()) return item;

    auto const meta = parsed.unwrap();
    auto str = [&meta](char const* key) {
        return meta.contains(key) ? meta[key].asString().unwrapOr("") : std::string();
    };
    auto num = [&meta](char const* key) {
        return meta.contains(key) ? static_cast<int>(meta[key].asInt().unwrapOr(0)) : 0;
    };
    auto flag = [&meta](char const* key) {
        return meta.contains(key) && meta[key].asBool().unwrapOr(false);
    };

    item.levelName = str("levelName");
    item.creator = str("creatorName");
    item.stars = num("stars");
    item.coins = num("coins");
    item.downloads = num("downloads");
    item.likes = num("likes");
    item.verifiedCoins = flag("coinsVerified");
    item.difficulty = difficultyName(flag("autoLevel"), flag("demon"),
                                     num("demonDifficulty"), item.stars);

    static constexpr std::array kLengths{"Tiny", "Short", "Medium", "Long", "XL", "Plat."};
    int const length = num("levelLength");
    if (length >= 0 && length < static_cast<int>(kLengths.size())) item.length = kLengths[length];
    if (flag("isPlatformer")) item.length = "Plat.";

    // m_isEpic is a tier, not a flag: 1 epic, 2 legendary, 3 mythic.
    int const epicTier = num("isEpic");
    if (epicTier >= 1 && epicTier <= 3) item.rateTier = epicTier + 1;
    else if (num("featured") > 0) item.rateTier = 1;

    return item;
}

} // namespace

Config readConfig() {
    Config config;
    config.enabled = Mod::get()->getSettingValue<bool>("thumbalert-enabled");
    config.spot = static_cast<Spot>(pickIndex("thumbalert-position", kSpotNames,
                                              static_cast<int>(Spot::TopRight)));
    config.enter = static_cast<Enter>(pickIndex("thumbalert-anim-in", kEnterNames,
                                                static_cast<int>(Enter::Slide)));
    config.exit = static_cast<Exit>(pickIndex("thumbalert-anim-out", kExitNames,
                                              static_cast<int>(Exit::Slide)));
    config.idle = static_cast<Idle>(pickIndex("thumbalert-anim-idle", kIdleNames,
                                              static_cast<int>(Idle::Float)));
    config.sound = static_cast<Sound>(pickIndex("thumbalert-sound", kSoundNames,
                                                static_cast<int>(Sound::Soft)));
    config.enterTime = settingFloat("thumbalert-anim-in-time", 0.55f, 0.1f, 2.f);
    config.exitTime = settingFloat("thumbalert-anim-out-time", 0.4f, 0.1f, 2.f);
    config.hold = settingFloat("thumbalert-duration", 5.f, 1.f, 15.f);
    config.scale = settingFloat("thumbalert-scale", 1.f, kMinScale, kMaxScale);
    config.gap = settingFloat("thumbalert-gap", 0.6f, 0.f, 5.f);
    config.offsetX = settingFloat("thumbalert-offset-x", 0.f, -160.f, 160.f);
    config.offsetY = settingFloat("thumbalert-offset-y", 0.f, -110.f, 110.f);
    config.dim = settingInt("thumbalert-dim", 0, 255);
    config.kenBurns = Mod::get()->getSettingValue<bool>("thumbalert-kenburns");
    config.shine = Mod::get()->getSettingValue<bool>("thumbalert-shine");
    config.progress = Mod::get()->getSettingValue<bool>("thumbalert-progress");
    config.stats = Mod::get()->getSettingValue<bool>("thumbalert-stats");
    config.click = Mod::get()->getSettingValue<bool>("thumbalert-click");
    config.maxBatch = settingInt("thumbalert-max-batch", 1, 10);
    config.whilePlaying = Mod::get()->getSettingValue<bool>("thumbalert-while-playing");
    config.whileEditing = Mod::get()->getSettingValue<bool>("thumbalert-while-editing");

    // Honour the accessibility switch the rest of the mod already respects.
    if (Mod::get()->getSavedValue<bool>("smooth-ui-reduced-motion", false)) {
        config.enter = Enter::None;
        config.exit = Exit::Fade;
        config.idle = Idle::None;
        config.kenBurns = false;
        config.shine = false;
        config.enterTime = 0.1f;
    }
    return config;
}

bool alertsAllowedHere(Config const& config) {
    if (PlayLayer::get() && !config.whilePlaying) return false;
    if (LevelEditorLayer::get() && !config.whileEditing) return false;
    return true;
}

void showThumbAlert(NewThumb item) {
    enqueue(std::move(item), false);
}

void showThumbAlertForUpload(int levelId, std::string const& uploader,
                             std::string const& levelMeta,
                             std::string const& serverMessage) {
    if (levelId <= 0) return;
    // Suggestions sit in the moderation queue; the server only publishes them
    // once a moderator accepts, so announcing one here would be a lie.
    if (serverMessage.find("pending") != std::string::npos ||
        serverMessage.find("verification") != std::string::npos) {
        return;
    }

    auto item = thumbFromLevelMeta(levelId, levelMeta);
    item.uploader = uploader;
    // The poll would otherwise show this same upload a second time.
    NewThumbWatcher::get().suppressLevel(levelId);
    // You asked for this one, so it is not the interruption the scene filters
    // exist to prevent.
    enqueue(std::move(item), true);
}

void showThumbAlertPreview() {
    if (paimon::isRuntimeShuttingDown()) return;

    NewThumb demo;
    demo.levelName = "Level Name";
    demo.creator = "Creator";
    demo.uploader = "You";
    if (auto* account = GJAccountManager::sharedState(); account && !account->m_username.empty()) {
        demo.uploader = std::string(account->m_username);
    }
    demo.difficulty = "Extreme Demon";
    demo.length = "XL";
    demo.stars = 10;
    demo.coins = 3;
    demo.verifiedCoins = true;
    demo.downloads = 1284510;
    demo.likes = 92310;
    demo.rateTier = 3;

    // Borrow a thumbnail already on disk so the preview shows the real thing.
    auto const owned = LocalThumbs::get().getAllLevelIDs();
    if (!owned.empty()) demo.levelId = owned.front();

    queue().clear();
    ++g_generation;
    g_showing = false;
    queue().push_back(std::move(demo));
    pump();
}

} // namespace paimon::thumbalerts
