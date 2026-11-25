#include "../../../utils/FilesystemPath.hpp"
#include "TransitionManager.hpp"
#include "DynamicTransitionManager.hpp"
#include "NativeSceneNavigation.hpp"
#include "TransitionTimeline.hpp"
#include "TransitionEvaluation.hpp"
#include "../ui/CustomTransitionScene.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/loader/Log.hpp>
#include <matjson.hpp>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

// backdrop for native transitions, which may expose the raw gl clear color
// while neither scene covers the screen.
static void attachTransitionBackdrop(CCScene* trans) {
    if (!trans) return;
    if (trans->getChildByID("paimon-transition-backdrop"_spr)) return;

    auto winSize = CCDirector::get()->getWinSize();
    CCNode* backdrop = nullptr;
    if (auto grad = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        grad->setAnchorPoint({0.f, 0.f});
        grad->setScaleX(winSize.width / grad->getContentSize().width);
        grad->setScaleY(winSize.height / grad->getContentSize().height);
        grad->setColor({0, 102, 255});
        backdrop = grad;
    } else {
        backdrop = CCLayerColor::create({0, 102, 255, 255}, winSize.width, winSize.height);
    }
    if (backdrop) {
        backdrop->setID("paimon-transition-backdrop"_spr);
        trans->addChild(backdrop, -100);
    }
}

TransitionManager::TransitionManager() {
    m_globalConfig.type = TransitionType::Fade;
    m_globalConfig.duration = 0.5f;
}

TransitionManager& TransitionManager::get() {
    static TransitionManager instance;
    return instance;
}

void TransitionManager::tripCustomSafeMode(std::string const& reason) {
    if (!m_customSafeModeTripped) {
        log::warn("[TransitionManager] Custom safe mode activated: {}", reason);
    }
    m_customSafeModeTripped = true;
}

namespace {

// single source for the type <-> string mappings; allTypes() order is the
// table order. fallbacks stay per-function (unknown id -> Fade, unknown
// type -> "fade" / "Fade" / "").
struct TransitionTypeInfo {
    TransitionType type;
    char const* id;
    char const* displayName;
    char const* description;
};

constexpr TransitionTypeInfo kTransitionTypes[] = {
    {TransitionType::Fade, "fade", "Fade (Black)", "Fades to black, then reveals the new screen."},
    {TransitionType::FadeWhite, "fade_white", "Fade (White)", "Fades to white, then reveals the new screen."},
    {TransitionType::FadeColor, "fade_color", "Fade (Color)", "Fades to a color you pick, then reveals."},
    {TransitionType::CrossFade, "crossfade", "Cross Fade", "Both screens blend together smoothly."},
    {TransitionType::SlideLeft, "slide_left", "Slide Left", "New screen slides in from the left."},
    {TransitionType::SlideRight, "slide_right", "Slide Right", "New screen slides in from the right."},
    {TransitionType::SlideUp, "slide_up", "Slide Up", "New screen slides in from the top."},
    {TransitionType::SlideDown, "slide_down", "Slide Down", "New screen slides in from the bottom."},
    {TransitionType::MoveInLeft, "move_in_left", "Move In Left", "New screen moves over the old one from left."},
    {TransitionType::MoveInRight, "move_in_right", "Move In Right", "New screen moves over the old one from right."},
    {TransitionType::MoveInTop, "move_in_top", "Move In Top", "New screen moves over the old one from top."},
    {TransitionType::MoveInBottom, "move_in_bottom", "Move In Bottom", "New screen moves over the old one from bottom."},
    {TransitionType::ZoomIn, "zoom_in", "Zoom Flip In", "3D zoom flip to reveal the new screen."},
    {TransitionType::ZoomOut, "zoom_out", "Zoom Flip Out", "3D zoom flip outward to the new screen."},
    {TransitionType::ZoomFlipYUp, "zoom_flip_y_up", "Zoom Flip Y Up", "3D zoom flip upward along Y axis."},
    {TransitionType::ZoomFlipYDown, "zoom_flip_y_down", "Zoom Flip Y Down", "3D zoom flip downward along Y axis."},
    {TransitionType::FlipX, "flip_x", "Flip Horizontal", "Flips horizontally like a card."},
    {TransitionType::FlipY, "flip_y", "Flip Vertical", "Flips vertically like a card."},
    {TransitionType::FlipAngular, "flip_angular", "Flip Angular", "Flips diagonally for a dynamic look."},
    {TransitionType::ShrinkGrow, "shrink_grow", "Shrink & Grow", "Old screen shrinks, new one grows in."},
    {TransitionType::RotoZoom, "roto_zoom", "Roto Zoom", "Rotates and zooms between screens."},
    {TransitionType::JumpZoom, "jump_zoom", "Jump Zoom", "Jumps and zooms to the new screen."},
    {TransitionType::FadeTR, "fade_tr", "Tiles (Top-Right)", "Tiles appear from top-right corner."},
    {TransitionType::FadeBL, "fade_bl", "Tiles (Bottom-Left)", "Tiles appear from bottom-left corner."},
    {TransitionType::FadeUp, "fade_up", "Tiles (Upward)", "Tiles appear sweeping upward."},
    {TransitionType::FadeDown, "fade_down", "Tiles (Downward)", "Tiles appear sweeping downward."},
    {TransitionType::TurnOffTiles, "turn_off_tiles", "Turn Off Tiles", "Tiles randomly disappear like a TV off."},
    {TransitionType::SplitCols, "split_cols", "Split Columns", "Screen splits into vertical columns."},
    {TransitionType::SplitRows, "split_rows", "Split Rows", "Screen splits into horizontal rows."},
    {TransitionType::ProgressRadialCW, "progress_cw", "Radial CW", "Circular wipe clockwise, like a clock."},
    {TransitionType::ProgressRadialCCW, "progress_ccw", "Radial CCW", "Circular wipe counter-clockwise."},
    {TransitionType::ProgressInOut, "progress_inout", "Circle Iris", "Circle iris opens/closes like a camera."},
    {TransitionType::ProgressOutIn, "progress_outin", "Circle Iris Out", "Circle starts open and closes inward."},
    {TransitionType::ProgressHorizontal, "progress_h", "Horizontal Wipe", "Horizontal bar wipe from left to right."},
    {TransitionType::ProgressVertical, "progress_v", "Vertical Wipe", "Vertical bar wipe from top to bottom."},
    {TransitionType::PageForward, "page_forward", "Page Curl ->", "Page curls forward like a book."},
    {TransitionType::PageBackward, "page_backward", "Page Curl <-", "Page curls backward."},
    {TransitionType::FadeBounce, "fade_bounce", "Fade Bounce", "Fades to black, new screen bounces in."},
    {TransitionType::SlideOverLeft, "slide_over_left", "Slide Over Left", "Old slides out left, new slides in from right."},
    {TransitionType::SlideOverRight, "slide_over_right", "Slide Over Right", "Old slides out right, new slides in from left."},
    {TransitionType::SlideOverUp, "slide_over_up", "Slide Over Up", "Old slides up and away, new enters from below."},
    {TransitionType::SlideOverDown, "slide_over_down", "Slide Over Down", "Old slides down and away, new enters from above."},
    {TransitionType::ZoomShrinkFade, "zoom_shrink_fade", "Zoom Shrink Fade", "Old shrinks and fades, new zooms into view."},
    {TransitionType::SpinCW, "spin_cw", "Spin CW", "Scene spins clockwise while transitioning."},
    {TransitionType::SpinCCW, "spin_ccw", "Spin CCW", "Scene spins counter-clockwise while transitioning."},
    {TransitionType::PixelateFade, "pixelate_fade", "Pixelate Fade", "Random tiles dissolve in a mosaic pattern."},
    {TransitionType::DiamondWipe, "diamond_wipe", "Diamond Wipe", "Diamond-shaped iris reveals new screen."},
    {TransitionType::DoubleDoor, "double_door", "Double Door", "Screen splits open like double doors."},
    {TransitionType::Blinds, "blinds", "Venetian Blinds", "Venetian blinds effect between scenes."},
    {TransitionType::Swirl, "swirl", "Swirl Zoom", "Spiral zoom swirl into the new screen."},
    {TransitionType::Glitch, "glitch", "Glitch", "Quick shake + crossfade glitch effect."},
    {TransitionType::CinematicBars, "cinematic_bars", "Cinematic Bars", "Letterbox bars fade in, then reveal new scene."},
    {TransitionType::FlashWhite, "flash_white", "Flash White", "Quick white flash, then reveals the new screen."},
    {TransitionType::HeartIris, "heart_iris", "Heart Iris", "Fast circle iris with a soft, cute feel."},
    {TransitionType::WaveSlide, "wave_slide", "Wave Slide", "New screen slides in with a wave-like motion."},
    {TransitionType::Random, "random", "Random!", "A random transition is picked every time!"},
    {TransitionType::Custom, "custom", "Custom (DSL)", "Define your own transition with commands!"},
    {TransitionType::Stinger, "stinger", "Stinger (OBS)", "Image, GIF or video overlay with a configurable scene cut"},
    {TransitionType::None, "none", "None (Instant)", "Instant switch, no animation."},
};

TransitionTypeInfo const* findTypeInfo(TransitionType type) {
    for (auto const& info : kTransitionTypes) {
        if (info.type == type) return &info;
    }
    return nullptr;
}

} // namespace

std::vector<TransitionType> const& TransitionManager::allTypes() {
    static const std::vector<TransitionType> types = [] {
        std::vector<TransitionType> out;
        out.reserve(sizeof(kTransitionTypes) / sizeof(kTransitionTypes[0]));
        for (auto const& info : kTransitionTypes) out.push_back(info.type);
        return out;
    }();
    return types;
}

TransitionType TransitionManager::typeFromString(std::string const& s) {
    for (auto const& info : kTransitionTypes) {
        if (s == info.id) return info.type;
    }
    return TransitionType::Fade;
}

std::string TransitionManager::typeToString(TransitionType t) {
    if (auto const* info = findTypeInfo(t)) return info->id;
    return "fade";
}

std::string TransitionManager::typeDisplayName(TransitionType t) {
    if (auto const* info = findTypeInfo(t)) return info->displayName;
    return "Fade";
}

std::string TransitionManager::typeDescription(TransitionType t) {
    if (auto const* info = findTypeInfo(t)) return info->description;
    return "";
}

CommandAction TransitionManager::actionFromString(std::string const& s) {
    if (s == "fade_out")  return CommandAction::FadeOut;
    if (s == "fade_in")   return CommandAction::FadeIn;
    if (s == "move")      return CommandAction::Move;
    if (s == "scale")     return CommandAction::Scale;
    if (s == "rotate")    return CommandAction::Rotate;
    if (s == "wait")      return CommandAction::Wait;
    if (s == "color")     return CommandAction::Color;
    if (s == "ease_in")   return CommandAction::EaseIn;
    if (s == "ease_out")  return CommandAction::EaseOut;
    if (s == "spawn")     return CommandAction::Spawn;
    if (s == "image")     return CommandAction::Image;
    if (s == "shake")     return CommandAction::Shake;
    if (s == "bounce")    return CommandAction::Bounce;
    if (s == "skew")      return CommandAction::Skew;
    if (s == "overlay")   return CommandAction::Overlay;
    if (s == "follow_cursor") return CommandAction::FollowCursor;
    return CommandAction::Wait;
}

std::string TransitionManager::actionToString(CommandAction a) {
    switch (a) {
        case CommandAction::FadeOut:    return "fade_out";
        case CommandAction::FadeIn:     return "fade_in";
        case CommandAction::Move:       return "move";
        case CommandAction::Scale:      return "scale";
        case CommandAction::Rotate:     return "rotate";
        case CommandAction::Wait:       return "wait";
        case CommandAction::Color:      return "color";
        case CommandAction::EaseIn:     return "ease_in";
        case CommandAction::EaseOut:    return "ease_out";
        case CommandAction::Spawn:      return "spawn";
        case CommandAction::Image:      return "image";
        case CommandAction::Shake:      return "shake";
        case CommandAction::Bounce:     return "bounce";
        case CommandAction::Skew:       return "skew";
        case CommandAction::Overlay:    return "overlay";
        case CommandAction::FollowCursor: return "follow_cursor";
    }
    return "wait";
}

TransitionEasing TransitionManager::easingFromString(std::string const& s) {
    if (s == "ease_in") return TransitionEasing::EaseIn;
    if (s == "ease_out") return TransitionEasing::EaseOut;
    if (s == "ease_in_out") return TransitionEasing::EaseInOut;
    if (s == "sine") return TransitionEasing::Sine;
    if (s == "back") return TransitionEasing::Back;
    if (s == "bounce") return TransitionEasing::Bounce;
    if (s == "elastic") return TransitionEasing::Elastic;
    return TransitionEasing::Linear;
}

std::string TransitionManager::easingToString(TransitionEasing e) {
    switch (e) {
        case TransitionEasing::EaseIn: return "ease_in";
        case TransitionEasing::EaseOut: return "ease_out";
        case TransitionEasing::EaseInOut: return "ease_in_out";
        case TransitionEasing::Sine: return "sine";
        case TransitionEasing::Back: return "back";
        case TransitionEasing::Bounce: return "bounce";
        case TransitionEasing::Elastic: return "elastic";
        default: return "linear";
    }
}

std::string TransitionManager::easingDisplayName(TransitionEasing e) {
    switch (e) {
        case TransitionEasing::EaseIn: return "Ease In";
        case TransitionEasing::EaseOut: return "Ease Out";
        case TransitionEasing::EaseInOut: return "Ease In+Out";
        case TransitionEasing::Sine: return "Sine";
        case TransitionEasing::Back: return "Back";
        case TransitionEasing::Bounce: return "Bounce";
        case TransitionEasing::Elastic: return "Elastic";
        default: return "Linear";
    }
}

std::vector<TransitionEasing> const& TransitionManager::allEasings() {
    static const std::vector<TransitionEasing> easings = {
        TransitionEasing::Linear, TransitionEasing::EaseIn, TransitionEasing::EaseOut,
        TransitionEasing::EaseInOut, TransitionEasing::Sine, TransitionEasing::Back,
        TransitionEasing::Bounce, TransitionEasing::Elastic,
    };
    return easings;
}

float TransitionManager::easeValue(TransitionEasing e, float t) {
    return paimon::transitions::transitionEase(e, t);
}

bool TransitionManager::isValidTarget(std::string const& target) {
    return target == "from" || target == "to" || target == "both";
}

int TransitionManager::sanitizeCommand(TransitionCommand& cmd) {
    int fixes = 0;

    auto sanitizeFloat = [&fixes](float& value, float fallback) {
        if (!paimon::transitions::finite(value)) {
            value = fallback;
            fixes++;
        }
    };

    auto clampCounted = [&fixes](auto& field, auto lo, auto hi) {
        auto clamped = std::clamp(field, lo, hi);
        if (clamped != field) {
            field = clamped;
            fixes++;
        }
    };

    sanitizeFloat(cmd.duration, 0.3f);
    sanitizeFloat(cmd.startTime, -1.f);
    sanitizeFloat(cmd.objScale, 1.f);
    cmd.startTime = std::clamp(cmd.startTime, -1.f, 30.f);
    if (cmd.objectName.size() > 48) cmd.objectName.resize(48);
    sanitizeFloat(cmd.delay, 0.f);
    sanitizeFloat(cmd.fromX, 0.f);
    sanitizeFloat(cmd.fromY, 0.f);
    sanitizeFloat(cmd.toX, 0.f);
    sanitizeFloat(cmd.toY, 0.f);
    sanitizeFloat(cmd.fromVal, 1.f);
    sanitizeFloat(cmd.toVal, 1.f);
    sanitizeFloat(cmd.intensity, 5.f);

    if (!isValidTarget(cmd.target)) {
        cmd.target = "from";
        fixes++;
    }

    clampCounted(cmd.duration, 0.01f, 30.f);
    clampCounted(cmd.delay, 0.f, 10.f);

    if (cmd.action == CommandAction::Scale) {
        clampCounted(cmd.fromVal, 0.01f, 10.f);
        clampCounted(cmd.toVal, 0.01f, 10.f);
    } else if (
        cmd.action == CommandAction::FadeOut ||
        cmd.action == CommandAction::FadeIn ||
        cmd.action == CommandAction::EaseIn ||
        cmd.action == CommandAction::EaseOut ||
        cmd.action == CommandAction::Bounce
    ) {
        clampCounted(cmd.fromVal, 0.f, 255.f);
        clampCounted(cmd.toVal, 0.f, 255.f);
    }

    if (cmd.action == CommandAction::Color) {
        clampCounted(cmd.r, 0, 255);
        clampCounted(cmd.g, 0, 255);
        clampCounted(cmd.b, 0, 255);
    }

    if (cmd.action == CommandAction::Shake) {
        clampCounted(cmd.intensity, 0.5f, 50.f);
    }

    if (cmd.action == CommandAction::Spawn) {
        clampCounted(cmd.spawnCount, 0, 16);
    }

    if (cmd.action == CommandAction::Skew) {
        clampCounted(cmd.fromVal, -60.f, 60.f);
        clampCounted(cmd.toVal, -60.f, 60.f);
    }
    if (cmd.action == CommandAction::Overlay) {
        clampCounted(cmd.fromVal, 0.f, 255.f);
        clampCounted(cmd.toVal, 0.f, 255.f);
        clampCounted(cmd.r, 0, 255);
        clampCounted(cmd.g, 0, 255);
        clampCounted(cmd.b, 0, 255);
    }
    if (cmd.action == CommandAction::Image) {
        // object position is a screen fraction; 0,0 in old files means unset.
        if (cmd.startTime < 0.f && cmd.fromX == 0.f && cmd.fromY == 0.f) { cmd.fromX = .5f; cmd.fromY = .5f; fixes++; }
        clampCounted(cmd.fromX, 0.f, 1.f);
        clampCounted(cmd.fromY, 0.f, 1.f);
        clampCounted(cmd.objScale, .05f, 4.f);
        cmd.fromVal = std::clamp(cmd.fromVal, 0.f, 255.f);
        cmd.toVal = std::clamp(cmd.toVal, 0.f, 255.f);
        cmd.r = std::clamp(cmd.r, 0, 255); cmd.g = std::clamp(cmd.g, 0, 255); cmd.b = std::clamp(cmd.b, 0, 255);
    }
    if (cmd.targetObj < -1) { cmd.targetObj = -1; fixes++; }
    if (static_cast<int>(cmd.easing) < 0 || static_cast<int>(cmd.easing) > 7) {
        cmd.easing = TransitionEasing::Linear;
        fixes++;
    }

    return fixes;
}

int TransitionManager::sanitizeCommands(std::vector<TransitionCommand>& commands) {
    int fixes = 0;
    if (commands.size() > 256) { commands.resize(256); ++fixes; }
    for (auto& cmd : commands) {
        fixes += sanitizeCommand(cmd);
    }
    // object references must point at an Image command other than themselves.
    for (std::size_t i = 0; i < commands.size(); ++i) {
        int obj = commands[i].targetObj;
        if (obj < 0) continue;
        if (obj >= (int)commands.size() || obj == (int)i ||
            commands[obj].action != CommandAction::Image) {
            commands[i].targetObj = -1;
            ++fixes;
        }
    }

    if (commands.empty()) {
        TransitionCommand fade;
        fade.action = CommandAction::FadeOut; fade.duration = .15f; fade.fromVal = 255.f; fade.toVal = 0.f;
        commands.push_back(fade);
        fade.action = CommandAction::FadeIn; fade.target = "to"; fade.fromVal = 0.f; fade.toVal = 255.f;
        commands.push_back(fade);
        fixes += 2;
    }
    return fixes;
}

int TransitionManager::sanitizeConfig(TransitionConfig& cfg) {
    cfg.cutPoint = paimon::transitions::bounded(cfg.cutPoint, .5f, 0.f, 1.f);
    int fixes = 0;
    if (!paimon::transitions::finite(cfg.duration) || cfg.duration < 0.01f) {
        cfg.duration = 0.5f;
        fixes++;
    } else if (cfg.duration > 30.f) {
        cfg.duration = 30.f;
        fixes++;
    }

    int nr = std::clamp(cfg.colorR, 0, 255);
    int ng = std::clamp(cfg.colorG, 0, 255);
    int nb = std::clamp(cfg.colorB, 0, 255);
    if (nr != cfg.colorR) { cfg.colorR = nr; fixes++; }
    if (ng != cfg.colorG) { cfg.colorG = ng; fixes++; }
    if (nb != cfg.colorB) { cfg.colorB = nb; fixes++; }

    if (cfg.type == TransitionType::Custom) {
        fixes += sanitizeCommands(cfg.commands);
    }

    return fixes;
}


std::filesystem::path TransitionManager::getConfigPath() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "transitions.json";
}


static TransitionCommand parseCommand(matjson::Value const& obj) {
    TransitionCommand cmd;
    if (!obj.isObject()) return cmd;
    cmd.action = TransitionManager::actionFromString(obj["action"].asString().unwrapOr("wait"));
    cmd.target = obj["target"].asString().unwrapOr("from");
    cmd.duration = paimon::json::floatOr(obj["duration"], cmd.duration);
    cmd.fromX = paimon::json::floatOr(obj["from_x"], cmd.fromX);
    cmd.fromY = paimon::json::floatOr(obj["from_y"], cmd.fromY);
    cmd.toX = paimon::json::floatOr(obj["to_x"], cmd.toX);
    cmd.toY = paimon::json::floatOr(obj["to_y"], cmd.toY);
    cmd.fromVal = paimon::json::floatOr(obj["from_val"], cmd.fromVal);
    cmd.toVal = paimon::json::floatOr(obj["to_val"], cmd.toVal);
    cmd.r = paimon::json::integerOr<int>(obj["r"]);
    cmd.g = paimon::json::integerOr<int>(obj["g"]);
    cmd.b = paimon::json::integerOr<int>(obj["b"]);
    cmd.imagePath = obj["image"].asString().unwrapOr("");
    cmd.spawnCount = paimon::json::integerOr<int>(obj["spawn_count"]);
    cmd.delay = paimon::json::floatOr(obj["delay"], cmd.delay);
    cmd.intensity = paimon::json::floatOr(obj["intensity"], cmd.intensity);
    cmd.easing = TransitionManager::easingFromString(obj["easing"].asString().unwrapOr("linear"));
    cmd.enabled = obj["enabled"].asBool().unwrapOr(true);
    cmd.targetObj = paimon::json::integerOr<int>(obj["obj"], -1);
    cmd.objScale = paimon::json::floatOr(obj["oscale"], cmd.objScale);
    cmd.startTime = paimon::json::floatOr(obj["start"], -1.f);
    cmd.solidObject = obj["solid"].asBool().unwrapOr(false);
    cmd.stretchMedia = obj["stretch_media"].asBool().unwrapOr(false);
    cmd.objectName = obj["name"].asString().unwrapOr("");
    return cmd;
}

static TransitionConfig parseConfig(matjson::Value const& obj) {
    TransitionConfig cfg;
    if (!obj.isObject()) return cfg;
    cfg.type = TransitionManager::typeFromString(obj["type"].asString().unwrapOr("fade"));
    cfg.duration = paimon::json::floatOr(obj["duration"], cfg.duration);
    auto const& color = obj["color"];
    if (color.isArray() && color.size() >= 3) {
        cfg.colorR = paimon::json::integerOr<int>(color[0]);
        cfg.colorG = paimon::json::integerOr<int>(color[1]);
        cfg.colorB = paimon::json::integerOr<int>(color[2]);
    }
    cfg.mediaPath = obj["media"].asString().unwrapOr("");
    cfg.cutPoint = paimon::json::floatOr(obj["cut_point"], cfg.cutPoint);
    cfg.sceneCut = obj["scene_cut"].asBool().unwrapOr(false);
    cfg.customName = obj["custom_name"].asString().unwrapOr("");
    cfg.scriptPath = obj["script"].asString().unwrapOr("");
    if (obj["commands"].isArray()) {
        cfg.commands.reserve(std::min<size_t>(256, obj["commands"].size()));
        for (auto const& c : obj["commands"]) {
            if (cfg.commands.size() >= 256) break;
            if (!c.isObject()) continue;
            cfg.commands.push_back(parseCommand(c));
        }
    }
    return cfg;
}

static bool migrateConfigImages(TransitionConfig& cfg) {
    bool changed = false;

    for (auto& cmd : cfg.commands) {
        if (cmd.imagePath.empty()) continue;
        auto extension = paimon::paths::toUtf8(paimon::assets::pathFromUtf8(cmd.imagePath).extension());
        if (geode::utils::string::toLower(extension) == ".pttransition") continue;
        auto imported = paimon::assets::importStoredPath(cmd.imagePath, "transitions", paimon::assets::Kind::Image);
        if (imported.success && !imported.path.empty()) {
            auto normalized = paimon::assets::normalizePathString(imported.path);
            if (normalized != cmd.imagePath) {
                cmd.imagePath = normalized;
                changed = true;
            }
        }
    }

    return changed;
}

static matjson::Value commandToJson(TransitionCommand const& cmd) {
    matjson::Value obj = matjson::makeObject({});
    obj.set("action",   TransitionManager::actionToString(cmd.action));
    obj.set("target",   cmd.target);
    obj.set("duration", static_cast<double>(cmd.duration));
    if (cmd.startTime >= 0.f) obj.set("start", static_cast<double>(cmd.startTime));
    if (cmd.solidObject) obj.set("solid", true);
    if (cmd.stretchMedia) obj.set("stretch_media", true);
    if (!cmd.objectName.empty()) obj.set("name", cmd.objectName);
    if (cmd.delay > 0.f) obj.set("delay", static_cast<double>(cmd.delay));
    if (cmd.action == CommandAction::Move || cmd.action == CommandAction::Image) {
        obj.set("from_x", static_cast<double>(cmd.fromX));
        obj.set("from_y", static_cast<double>(cmd.fromY));
        obj.set("to_x",   static_cast<double>(cmd.toX));
        obj.set("to_y",   static_cast<double>(cmd.toY));
    }
    if (cmd.action == CommandAction::Image) {
        obj.set("from_val", static_cast<double>(cmd.fromVal));
        obj.set("to_val",   static_cast<double>(cmd.toVal));
    }
    if (cmd.targetObj >= 0) obj.set("obj", cmd.targetObj);
    if (cmd.action == CommandAction::Image && cmd.objScale != 1.f)
        obj.set("oscale", static_cast<double>(cmd.objScale));
    if (cmd.action != CommandAction::Move && cmd.action != CommandAction::Wait &&
        cmd.action != CommandAction::Spawn &&
        cmd.action != CommandAction::Image) {
        obj.set("from_val", static_cast<double>(cmd.fromVal));
        obj.set("to_val",   static_cast<double>(cmd.toVal));
    }
    if (cmd.action == CommandAction::Move) { /* from/to already written above */ }
    if (cmd.action == CommandAction::Color || cmd.action == CommandAction::Overlay || cmd.solidObject) {
        obj.set("r", cmd.r); obj.set("g", cmd.g); obj.set("b", cmd.b);
    }
    if (cmd.action == CommandAction::Spawn) {
        obj.set("spawn_count", cmd.spawnCount);
    }
    if (cmd.action == CommandAction::Image && !cmd.imagePath.empty()) {
        obj.set("image", cmd.imagePath);
    }
    if (cmd.action == CommandAction::Shake) {
        obj.set("intensity", static_cast<double>(cmd.intensity));
    }
    if (cmd.easing != TransitionEasing::Linear) obj.set("easing", TransitionManager::easingToString(cmd.easing));
    if (!cmd.enabled) obj.set("enabled", false);
    return obj;
}

static matjson::Value configToJson(TransitionConfig const& cfg) {
    matjson::Value obj = matjson::makeObject({});
    obj.set("type", TransitionManager::typeToString(cfg.type));
    obj.set("media", cfg.mediaPath);
    obj.set("cut_point", static_cast<double>(cfg.cutPoint));
    if (cfg.sceneCut) obj.set("scene_cut", true);
    if (!cfg.customName.empty()) obj.set("custom_name", cfg.customName);
    obj.set("duration", static_cast<double>(cfg.duration));
    auto colorArr = matjson::Value::array();
    colorArr.push(cfg.colorR); colorArr.push(cfg.colorG); colorArr.push(cfg.colorB);
    obj.set("color", colorArr);
    if (!cfg.scriptPath.empty()) obj.set("script", cfg.scriptPath);
    if (!cfg.commands.empty()) {
        auto cmdsArr = matjson::Value::array();
        for (auto const& cmd : cfg.commands) cmdsArr.push(commandToJson(cmd));
        obj.set("commands", cmdsArr);
    }
    return obj;
}

std::string TransitionManager::commandsToJson(std::vector<TransitionCommand> const& commands) {
    auto arr = matjson::Value::array();
    for (auto const& cmd : commands) arr.push(commandToJson(cmd));
    auto root = matjson::makeObject({});
    root.set("paimbnails_transition", 1);
    root.set("commands", arr);
    return root.dump(matjson::TAB_INDENTATION);
}

bool TransitionManager::commandsFromJson(std::string const& text, std::vector<TransitionCommand>& out) {
    auto parseRes = matjson::parse(text);
    if (!parseRes) return false;
    auto const& root = parseRes.unwrap();
    matjson::Value const* arr = nullptr;
    if (root.isArray()) arr = &root;
    else if (root.isObject() && root["commands"].isArray()) arr = &root["commands"];
    else return false;
    std::vector<TransitionCommand> parsed;
    parsed.reserve(std::min<size_t>(256, arr->size()));
    for (auto const& c : *arr) {
        if (parsed.size() >= 256) break;
        if (!c.isObject()) continue;
        parsed.push_back(parseCommand(c));
    }
    if (parsed.empty()) return false;
    sanitizeCommands(parsed);
    out = std::move(parsed);
    return true;
}


void TransitionManager::loadConfig() {
    auto path = getConfigPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        m_loaded = true;
        saveConfig();
        return;
    }

    auto bytes = paimon::file::readBytes(path, 4 * 1024 * 1024);
    if (bytes.empty()) { m_loaded = true; return; }
    auto parseRes = matjson::parse(std::string(bytes.begin(), bytes.end()));
    if (!parseRes) { m_loaded = true; return; }

    auto const& root = parseRes.unwrap();
    if (!root.isObject()) {
        m_loaded = true;
        warmMedia();
        log::warn("[TransitionManager] Invalid config root type, using defaults");
        return;
    }

    if (root.contains("enabled"))
        m_enabled = root["enabled"].asBool().unwrapOr(true);

    if (root.contains("global") && root["global"].isObject())
        m_globalConfig = parseConfig(root["global"]);
    else if (root.contains("global"))
        log::warn("[TransitionManager] Invalid global config type, using defaults");

    if (root.contains("level_entry") && root["level_entry"].isObject()) {
        m_levelEntryConfig = parseConfig(root["level_entry"]);
        m_hasLevelEntryConfig = true;
        if (!m_levelEntryConfig.scriptPath.empty() && m_levelEntryConfig.commands.empty()) {
            m_levelEntryConfig.commands = parseScriptFile(m_levelEntryConfig.scriptPath);
            if (!m_levelEntryConfig.commands.empty()) m_levelEntryConfig.type = TransitionType::Custom;
        }
    } else if (root.contains("level_entry")) {
        log::warn("[TransitionManager] Invalid level_entry config type, ignoring");
    }

    if (!m_globalConfig.scriptPath.empty() && m_globalConfig.commands.empty()) {
        m_globalConfig.commands = parseScriptFile(m_globalConfig.scriptPath);
        if (!m_globalConfig.commands.empty()) m_globalConfig.type = TransitionType::Custom;
    }

    bool migratedImages = migrateConfigImages(m_globalConfig);
    if (m_hasLevelEntryConfig) {
        migratedImages = migrateConfigImages(m_levelEntryConfig) || migratedImages;
    }

    sanitizeConfig(m_globalConfig);
    if (m_hasLevelEntryConfig) {
        sanitizeConfig(m_levelEntryConfig);
    }

    m_loaded = true;
    warmMedia();
    if (migratedImages) {
        saveConfig();
    }
}

void TransitionManager::saveConfig() {
    migrateConfigImages(m_globalConfig);
    if (m_hasLevelEntryConfig) {
        migrateConfigImages(m_levelEntryConfig);
    }

    warmMedia();
    matjson::Value root = matjson::makeObject({});
    root.set("enabled", m_enabled);
    root.set("global", configToJson(m_globalConfig));
    if (m_hasLevelEntryConfig) {
        root.set("level_entry", configToJson(m_levelEntryConfig));
    }

    auto str = root.dump(matjson::TAB_INDENTATION);
    auto res = geode::utils::file::writeString(paimon::paths::toSdkPath(getConfigPath()), str);
    if (!res) log::warn("[TransitionManager] Failed to save: {}", res.unwrapErr());
}


TransitionConfig TransitionManager::getLevelEntryConfig() const {
    return m_hasLevelEntryConfig ? m_levelEntryConfig : m_globalConfig;
}

bool TransitionManager::isCustomActive() const {
    // if the safe mode tripped, Custom fell back to a native fade and no
    // longer owns scene changes, so Dynamic may resume.
    return m_enabled && !m_customSafeModeTripped &&
        m_globalConfig.type == TransitionType::Custom;
}

bool TransitionManager::isCustomActiveFor(CCScene* dest) const {
    if (!m_enabled || m_customSafeModeTripped) return false;
    if (dest && m_hasLevelEntryConfig && dest->getChildByType<PlayLayer>(0))
        return m_levelEntryConfig.type == TransitionType::Custom;
    return m_globalConfig.type == TransitionType::Custom;
}

void TransitionManager::setGlobalConfig(TransitionConfig const& cfg) {
    m_globalConfig = cfg;
    sanitizeConfig(m_globalConfig);
    migrateConfigImages(m_globalConfig);
}

void TransitionManager::setLevelEntryConfig(TransitionConfig const& cfg) {
    m_levelEntryConfig = cfg;
    sanitizeConfig(m_levelEntryConfig);
    migrateConfigImages(m_levelEntryConfig);
    m_hasLevelEntryConfig = true;
}

void TransitionManager::clearLevelEntryConfig() {
    m_hasLevelEntryConfig = false;
    m_levelEntryConfig = TransitionConfig{};
}

// presets compile through the same timeline as the command editor:
// independent tracks run together within the configured duration.
std::vector<TransitionCommand> TransitionManager::buildPreviewCommands(TransitionType type, float dur) const {
    dur = paimon::transitions::bounded(dur, .5f, .05f, 30.f);
    float half = dur * .5f;
    auto size = CCDirector::get()->getWinSize();
    float cx = size.width * .5f, cy = size.height * .5f;
    std::vector<TransitionCommand> commands;
    auto spawn = [&](int count) {
        TransitionCommand c; c.action = CommandAction::Spawn; c.spawnCount = count;
        commands.push_back(c);
    };
    auto value = [&](CommandAction action, char const* target, float time, float from, float to) {
        TransitionCommand c; c.action = action; c.target = target; c.duration = time;
        c.fromVal = from; c.toVal = to; commands.push_back(c);
    };
    auto move = [&](char const* target, CCPoint from, CCPoint to) {
        TransitionCommand c; c.action = CommandAction::Move; c.target = target; c.duration = dur;
        c.fromX = from.x; c.fromY = from.y; c.toX = to.x; c.toY = to.y;
        commands.push_back(c);
    };
    switch (type) {
        case TransitionType::SlideOverLeft:
        case TransitionType::SlideOverRight:
        case TransitionType::SlideOverUp:
        case TransitionType::SlideOverDown:
        case TransitionType::WaveSlide: {
            CCPoint offset{size.width, 0};
            if (type == TransitionType::SlideOverRight) offset = CCPoint{-size.width, 0};
            if (type == TransitionType::SlideOverUp) offset = CCPoint{0, -size.height};
            if (type == TransitionType::SlideOverDown) offset = CCPoint{0, size.height};
            if (type == TransitionType::WaveSlide) offset.y = 30;
            spawn(3);
            value(CommandAction::FadeIn, "to", dur, 255, 255);
            move("from", {cx, cy}, CCPoint{cx, cy} - offset);
            move("to", CCPoint{cx, cy} + offset, {cx, cy});
            break;
        }
        case TransitionType::ZoomShrinkFade:
        case TransitionType::SpinCW:
        case TransitionType::SpinCCW:
        case TransitionType::Swirl: {
            bool rotate = type != TransitionType::ZoomShrinkFade;
            spawn(rotate ? 3 : 2);
            value(CommandAction::Scale, "from", half, 1, .1f);
            value(CommandAction::FadeOut, "from", half, 255, 0);
            if (rotate) value(CommandAction::Rotate, "from", half, 0,
                type == TransitionType::SpinCCW ? -180.f : type == TransitionType::Swirl ? 360.f : 180.f);
            spawn(2);
            value(CommandAction::Scale, "to", half, 1.2f, 1);
            value(CommandAction::FadeIn, "to", half, 0, 255);
            break;
        }
        case TransitionType::Glitch: {
            spawn(2);
            TransitionCommand shake; shake.action = CommandAction::Shake;
            shake.duration = half; shake.intensity = 12;
            commands.push_back(shake);
            value(CommandAction::FadeOut, "from", half, 255, 0);
            value(CommandAction::FadeIn, "to", half, 0, 255);
            break;
        }
        case TransitionType::CrossFade:
            spawn(2);
            value(CommandAction::FadeOut, "from", dur, 255, 0);
            value(CommandAction::FadeIn, "to", dur, 0, 255);
            break;
        case TransitionType::Custom:
        case TransitionType::Stinger:
        case TransitionType::None:
            break;
        default:
            value(CommandAction::FadeOut, "from", half, 255, 0);
            value(type == TransitionType::FadeBounce ? CommandAction::Bounce : CommandAction::FadeIn, "to", half, 0, 255);
            break;
    }
    return commands;
}


CCScene* TransitionManager::createStudioScene(std::vector<TransitionCommand> commands,
                                              CCScene* dest, bool isPush,
                                              TransitionConfig const& cfg, bool custom,
                                              bool logFallback) const {
    sanitizeCommands(commands);
    auto fromScene = CCDirector::get()->getRunningScene();
    if (fromScene && dest && !commands.empty()) {
        float cutPoint = -1.f;
        float minDuration = 0.f;
        if (custom) {
            bool const absolute = std::any_of(commands.begin(), commands.end(),
                [](auto const& c) { return c.startTime >= 0.f; });
            cutPoint = cfg.sceneCut ? cfg.cutPoint : -1.f;
            minDuration = (cfg.sceneCut || absolute) ? cfg.duration : 0.f;
        }
        if (auto* transScene = CustomTransitionScene::create(
                fromScene, dest, commands, isPush, cutPoint, minDuration)) {
            return transScene;
        }
    }
    if (logFallback) {
        log::debug("[Transitions] Custom media not ready or timeline invalid; using fade");
    }
    return CCTransitionFade::create(cfg.duration, dest);
}


CCScene* TransitionManager::createTransition(
    TransitionConfig const& cfg,
    CCScene* dest,
    bool isPush)
{
    if (!m_loaded) loadConfig();
    if (!dest) return nullptr;
    if (dest == CCDirector::get()->getRunningScene()) return dest;
    if (paimon::transitions::requiresNativeSceneNavigation(CCDirector::get()->getRunningScene(), dest)) {
        return dest;
    }

    if (cfg.type == TransitionType::None) return dest;
    if (CustomTransitionScene::isActive()) return dest;

    TransitionConfig safeCfg = cfg;
    sanitizeConfig(safeCfg);

    if (safeCfg.type == TransitionType::Random) {
        auto const& types = allTypes();
        std::vector<TransitionType> eligible;
        for (auto t : types) {
            if (t != TransitionType::Random && t != TransitionType::Custom && t != TransitionType::Stinger && t != TransitionType::None)
                eligible.push_back(t);
        }
        if (!eligible.empty()) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, static_cast<int>(eligible.size()) - 1);
            safeCfg.type = eligible[dist(rng)];
        } else {
            safeCfg.type = TransitionType::Fade;
        }
    }

    if (safeCfg.type == TransitionType::Stinger) {
        auto media = paimon::transitions::findTransitionMedia(safeCfg.mediaPath);
        if (media) {
            if (auto* scene = CustomTransitionScene::createStinger(dest, media, safeCfg.duration, safeCfg.cutPoint)) return scene;
        } else paimon::transitions::prepareTransitionMedia(safeCfg.mediaPath);
        return CCTransitionFade::create(safeCfg.duration, dest);
    }

    if (safeCfg.type == TransitionType::Custom) {
        if (m_customSafeModeTripped) {
            return CCTransitionFade::create(safeCfg.duration, dest);
        }

        auto commands = safeCfg.commands;
        if (commands.empty() && !safeCfg.scriptPath.empty())
            commands = parseScriptFile(safeCfg.scriptPath);
        return createStudioScene(std::move(commands), dest, isPush, safeCfg, true, true);
    }

    bool isCreativeType = false;
    switch (safeCfg.type) {
        case TransitionType::FadeBounce:
        case TransitionType::SlideOverLeft:
        case TransitionType::SlideOverRight:
        case TransitionType::SlideOverUp:
        case TransitionType::SlideOverDown:
        case TransitionType::ZoomShrinkFade:
        case TransitionType::SpinCW:
        case TransitionType::SpinCCW:
        case TransitionType::Swirl:
        case TransitionType::Glitch:
        case TransitionType::CinematicBars:
        case TransitionType::FlashWhite:
        case TransitionType::WaveSlide:
            isCreativeType = true;
            break;
        default:
            break;
    }

    if (isCreativeType) {
        if (m_customSafeModeTripped) {
            return CCTransitionFade::create(safeCfg.duration, dest);
        }
        return createStudioScene(buildPreviewCommands(safeCfg.type, safeCfg.duration),
            dest, isPush, safeCfg, false, false);
    }

    auto* trans = createNativeTransition(safeCfg, dest);
    if (trans) {
        attachTransitionBackdrop(trans);
        return static_cast<CCScene*>(trans);
    }
    return dest;
}

void TransitionManager::navigateScene(CCScene* dest, bool isPush) {
    if (!dest) return;
    auto* dir = CCDirector::get();
    if (auto* dynamic = paimon::transitions::dynamic::createTransition(dest)) {
        if (isPush) dir->pushScene(dynamic);
        else dir->replaceScene(dynamic);
        return;
    }

    CCScene* target = dest;
    if (m_enabled) {
        if (!m_loaded) loadConfig();
        auto* trans = createTransition(m_globalConfig, dest, isPush);
        target = trans ? trans : dest;
    }
    if (isPush) dir->pushScene(target);
    else dir->replaceScene(target);
}

void TransitionManager::replaceScene(CCScene* dest) { navigateScene(dest, false); }

void TransitionManager::pushScene(CCScene* dest) { navigateScene(dest, true); }

CCTransitionScene* TransitionManager::createNativeTransition(TransitionConfig const& cfg, CCScene* dest) const {
    if (!dest) return nullptr;
    float dur = paimon::transitions::bounded(cfg.duration, .5f, .01f, 30.f);
    ccColor3B col = {
        static_cast<GLubyte>(std::clamp(cfg.colorR, 0, 255)),
        static_cast<GLubyte>(std::clamp(cfg.colorG, 0, 255)),
        static_cast<GLubyte>(std::clamp(cfg.colorB, 0, 255))
    };

    switch (cfg.type) {
        case TransitionType::Fade:              return CCTransitionFade::create(dur, dest, {0, 0, 0});
        case TransitionType::FadeWhite:         return CCTransitionFade::create(dur, dest, {255, 255, 255});
        case TransitionType::FadeColor:         return CCTransitionFade::create(dur, dest, col);

#if !defined(GEODE_IS_IOS)
        case TransitionType::CrossFade:         return CCTransitionCrossFade::create(dur, dest);

        case TransitionType::SlideLeft:         return CCTransitionSlideInL::create(dur, dest);
        case TransitionType::SlideRight:        return CCTransitionSlideInR::create(dur, dest);
        case TransitionType::SlideUp:           return CCTransitionSlideInT::create(dur, dest);
        case TransitionType::SlideDown:         return CCTransitionSlideInB::create(dur, dest);
        case TransitionType::MoveInLeft:        return CCTransitionMoveInL::create(dur, dest);
        case TransitionType::MoveInRight:       return CCTransitionMoveInR::create(dur, dest);
        case TransitionType::MoveInTop:         return CCTransitionMoveInT::create(dur, dest);
        case TransitionType::MoveInBottom:      return CCTransitionMoveInB::create(dur, dest);

        case TransitionType::ZoomIn:            return CCTransitionZoomFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::ZoomOut:           return CCTransitionZoomFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationRightOver);
        case TransitionType::ZoomFlipYUp:       return CCTransitionZoomFlipY::create(dur, dest, tOrientation::kCCTransitionOrientationUpOver);
        case TransitionType::ZoomFlipYDown:     return CCTransitionZoomFlipY::create(dur, dest, tOrientation::kCCTransitionOrientationDownOver);
        case TransitionType::FlipX:             return CCTransitionFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::FlipY:             return CCTransitionFlipY::create(dur, dest, tOrientation::kCCTransitionOrientationUpOver);
        case TransitionType::FlipAngular:       return CCTransitionFlipAngular::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::ShrinkGrow:        return CCTransitionShrinkGrow::create(dur, dest);
        case TransitionType::RotoZoom:          return CCTransitionRotoZoom::create(dur, dest);
        case TransitionType::JumpZoom:          return CCTransitionJumpZoom::create(dur, dest);

        case TransitionType::FadeTR:            return CCTransitionFadeTR::create(dur, dest);
        case TransitionType::FadeBL:            return CCTransitionFadeBL::create(dur, dest);
        case TransitionType::FadeUp:            return CCTransitionFadeUp::create(dur, dest);
        case TransitionType::FadeDown:          return CCTransitionFadeDown::create(dur, dest);
        case TransitionType::TurnOffTiles:      return CCTransitionTurnOffTiles::create(dur, dest);
        case TransitionType::SplitCols:         return CCTransitionSplitCols::create(dur, dest);
        case TransitionType::SplitRows:         return CCTransitionSplitRows::create(dur, dest);
        case TransitionType::ProgressRadialCW:  return CCTransitionProgressRadialCW::create(dur, dest);
        case TransitionType::ProgressRadialCCW: return CCTransitionProgressRadialCCW::create(dur, dest);
        case TransitionType::ProgressInOut:     return CCTransitionProgressInOut::create(dur, dest);
        case TransitionType::ProgressOutIn:     return CCTransitionProgressOutIn::create(dur, dest);
        case TransitionType::ProgressHorizontal: return CCTransitionProgressHorizontal::create(dur, dest);
        case TransitionType::ProgressVertical:  return CCTransitionProgressVertical::create(dur, dest);

        case TransitionType::PageForward:       return CCTransitionPageTurn::create(dur, dest, false);
        case TransitionType::PageBackward:      return CCTransitionPageTurn::create(dur, dest, true);

        case TransitionType::PixelateFade:      return CCTransitionTurnOffTiles::create(dur, dest);
        case TransitionType::DiamondWipe:       return CCTransitionProgressInOut::create(dur, dest);
        case TransitionType::DoubleDoor:        return CCTransitionSplitCols::create(dur, dest);
        case TransitionType::Blinds:            return CCTransitionSplitRows::create(dur, dest);
        case TransitionType::HeartIris:         return CCTransitionProgressInOut::create(dur * 0.7f, dest);
#endif

        default:                                return CCTransitionFade::create(dur, dest, {0, 0, 0});
    }
}


std::vector<TransitionCommand> TransitionManager::parseScriptFile(std::string const& scriptPath) const {
    std::vector<TransitionCommand> commands;
    auto fullPath = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / paimon::assets::pathFromUtf8(scriptPath);
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) { log::warn("[TransitionManager] parseScriptFile: file not found"); return commands; }

    auto bytes = paimon::file::readBytes(fullPath, 1024 * 1024);
    if (bytes.empty()) return commands;
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string line;

    while (commands.size() < 256 && std::getline(stream, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line.erase(0, first);

        auto last = line.find_last_not_of(" \t\r\n");
        if (last == std::string::npos) continue;
        line.erase(last + 1);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ls(line);
        std::string verb;
        ls >> verb;
        verb = geode::utils::string::toLower(verb);

        TransitionCommand cmd;
        bool valid = true;

        if (verb == "fade_out") {
            cmd.action = CommandAction::FadeOut; cmd.target = "from";
            valid = static_cast<bool>(ls >> cmd.duration); cmd.fromVal = 255.f; cmd.toVal = 0.f;
        } else if (verb == "fade_in") {
            cmd.action = CommandAction::FadeIn; cmd.target = "to";
            valid = static_cast<bool>(ls >> cmd.duration); cmd.fromVal = 0.f; cmd.toVal = 255.f;
        } else if (verb == "move") {
            cmd.action = CommandAction::Move;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromX >> cmd.fromY >> cmd.toX >> cmd.toY >> cmd.duration);
        } else if (verb == "scale") {
            cmd.action = CommandAction::Scale;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "rotate") {
            cmd.action = CommandAction::Rotate;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "wait") {
            cmd.action = CommandAction::Wait;
            valid = static_cast<bool>(ls >> cmd.duration);
        } else if (verb == "ease_in") {
            cmd.action = CommandAction::EaseIn;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "ease_out") {
            cmd.action = CommandAction::EaseOut;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else {
            continue;
        }

        if (!valid) continue;
        TransitionManager::sanitizeCommand(cmd);
        commands.push_back(cmd);
    }
    return commands;
}

void TransitionManager::warmMedia() {
    auto generation = ++m_mediaGeneration;
    m_preloadedMedia.clear();
    auto prepare = [this, generation](std::string const& path) {
        if (path.empty()) return;
        paimon::transitions::prepareTransitionMedia(path, [this, generation](auto media, auto) {
            if (generation == m_mediaGeneration && media) m_preloadedMedia.push_back(std::move(media));
        });
    };
    for (auto const* cfg : {&m_globalConfig, &m_levelEntryConfig}) {
        if (cfg == &m_levelEntryConfig && !m_hasLevelEntryConfig) continue;
        if (cfg->type == TransitionType::Stinger) prepare(cfg->mediaPath);
        if (cfg->type == TransitionType::Custom) {
            for (auto const& command : cfg->commands) if (command.action == CommandAction::Image) prepare(command.imagePath);
        }
    }
}
