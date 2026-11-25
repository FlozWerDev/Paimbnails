#include "EditorMcpBridge.hpp"

#include "EditorMcpCatalog.hpp"
#include "EditorMcpRenderer.hpp"
#include "EditorMcpServer.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/Base64.hpp"
#include "../../utils/JsonHelper.hpp"
#include "../../utils/MainThread.hpp"
#include "../../utils/MainThreadDelay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/DrawGridLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSpriteColor.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editormcp {

namespace {

constexpr int kTextObjectID = 914;
constexpr int kMaxBatchObjects = 2500;
constexpr float kGroundOffsetY = 90.0f;
constexpr float kBlockSizeUnits = 30.0f;
constexpr auto kMainThreadTimeout = std::chrono::seconds(10);

matjson::Value textResult(std::string text, bool isError = false) {
    return matjson::makeObject({
        {"content", jsonArray({
            matjson::makeObject({{"type", "text"}, {"text", std::move(text)}})
        })},
        {"isError", isError},
    });
}

matjson::Value jsonResult(matjson::Value const& payload, bool isError = false) {
    return textResult(payload.dump(matjson::NO_INDENTATION), isError);
}

matjson::Value errorResult(std::string_view msg) {
    return jsonResult(matjson::makeObject({{"ok", false}, {"error", std::string(msg)}}), true);
}

// Run a callable on GD's main thread and wait cleanly with shutdown awareness.
template <typename Fn>
auto runOnMainThread(Fn&& fn) -> decltype(fn()) {
    using Ret = decltype(fn());
    if (paimon::isRuntimeShuttingDown()) {
        if constexpr (std::is_same_v<Ret, matjson::Value>) {
            return errorResult("Game is shutting down.");
        } else {
            return Ret{};
        }
    }
    if (paimon::isMainThread()) {
        return fn();
    }

    struct SharedState {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        Ret result{};
    };
    auto state = std::make_shared<SharedState>();

    Loader::get()->queueInMainThread([state, f = std::forward<Fn>(fn)]() mutable {
        Ret r = f();
        {
            std::lock_guard lock(state->mutex);
            state->result = std::move(r);
            state->done = true;
        }
        state->cv.notify_one();
    });

    auto const deadline = std::chrono::steady_clock::now() + kMainThreadTimeout;
    std::unique_lock lock(state->mutex);
    while (!state->done) {
        if (paimon::isRuntimeShuttingDown()) {
            if constexpr (std::is_same_v<Ret, matjson::Value>) {
                return errorResult("Aborted: game shutting down.");
            } else {
                return Ret{};
            }
        }
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            if constexpr (std::is_same_v<Ret, matjson::Value>) {
                return errorResult("Timed out waiting for GD main thread.");
            } else {
                return Ret{};
            }
        }
    }
    return std::move(state->result);
}

GJEffectManager* getEffectManager(LevelEditorLayer* lel) {
    if (!lel) return nullptr;
    if (lel->m_levelSettings && lel->m_levelSettings->m_effectManager) {
        return lel->m_levelSettings->m_effectManager;
    }
    return lel->m_effectManager;
}

std::string formatFloat(float v) {
    if (!std::isfinite(v)) return "0";
    std::string s = fmt::format("{:.4f}", v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string encodeUtf8Base64(std::string_view sv) {
    std::vector<uint8_t> bytes(sv.begin(), sv.end());
    return paimon::base64Encode(bytes);
}

std::string decodeUtf8Base64(std::string_view sv) {
    std::vector<uint8_t> bytes;
    if (!paimon::base64Decode(sv, bytes) || bytes.empty()) return {};
    return std::string(bytes.begin(), bytes.end());
}

struct SceneSwapWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;

    void notifyDone() {
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        cv.notify_all();
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [this] { return done; });
    }
};

bool parseHexColor(std::string_view hex, ccColor3B& out) {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    if (hex.size() != 6) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    int vals[6];
    for (int i = 0; i < 6; ++i) {
        vals[i] = nibble(hex[i]);
        if (vals[i] < 0) return false;
    }
    out.r = static_cast<GLubyte>((vals[0] << 4) | vals[1]);
    out.g = static_cast<GLubyte>((vals[2] << 4) | vals[3]);
    out.b = static_cast<GLubyte>((vals[4] << 4) | vals[5]);
    return true;
}

int parseZLayerValue(matjson::Value const& val, int fallback = 0) {
    if (val.isNumber()) return paimon::json::integerOr<int>(val, fallback);
    if (!val.isString()) return fallback;
    std::string s = val.asString().unwrapOr("");
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (s == "B5") return -5;
    if (s == "B4") return -3;
    if (s == "B3") return -1;
    if (s == "B2") return 1;
    if (s == "B1") return 3;
    if (s == "DEFAULT" || s == "DEF") return 0;
    if (s == "T1") return 5;
    if (s == "T2") return 7;
    if (s == "T3") return 9;
    if (s == "T4") return 11;
    return fallback;
}

char const* zLayerName(int z) {
    switch (z) {
        case -5: return "B5";
        case -3: return "B4";
        case -1: return "B3";
        case 1:  return "B2";
        case 3:  return "B1";
        case 0:  return "Default";
        case 5:  return "T1";
        case 7:  return "T2";
        case 9:  return "T3";
        case 11: return "T4";
        default: return "Custom";
    }
}

int parseEasingValue(matjson::Value const& val, int fallback = 0) {
    if (val.isNumber()) return paimon::json::integerOr<int>(val, fallback);
    if (!val.isString()) return fallback;
    std::string s;
    for (unsigned char c : val.asString().unwrapOr("")) {
        if (std::isalnum(c)) s.push_back(static_cast<char>(std::tolower(c)));
    }
    static std::pair<char const*, int> const kEasings[] = {
        {"none", 0}, {"linear", 0},
        {"easeinout", 1}, {"easein", 2}, {"easeout", 3},
        {"elasticinout", 4}, {"elasticin", 5}, {"elasticout", 6},
        {"bounceinout", 7}, {"bouncein", 8}, {"bounceout", 9},
        {"exponentialinout", 10}, {"exponentialin", 11}, {"exponentialout", 12},
        {"expoinout", 10}, {"expoin", 11}, {"expoout", 12},
        {"sineinout", 13}, {"sinein", 14}, {"sineout", 15},
        {"backinout", 16}, {"backin", 17}, {"backout", 18},
    };
    for (auto const& e : kEasings) {
        if (s == e.first) return e.second;
    }
    return fallback;
}

std::vector<int> objectGroups(GameObject* obj) {
    std::vector<int> out;
    if (!obj) return out;
    for (int i = 0; i < obj->m_groupCount; ++i) {
        int g = obj->getGroupID(i);
        if (g > 0) out.push_back(g);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> parseSavePairs(std::string_view save) {
    std::vector<std::pair<std::string, std::string>> pairs;
    if (!save.empty() && save.back() == ';') save.remove_suffix(1);
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start <= save.size()) {
        size_t comma = save.find(',', start);
        if (comma == std::string_view::npos) {
            tokens.emplace_back(save.substr(start));
            break;
        }
        tokens.emplace_back(save.substr(start, comma - start));
        start = comma + 1;
    }
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        pairs.emplace_back(tokens[i], tokens[i + 1]);
    }
    return pairs;
}

std::string findSavePairValue(std::vector<std::pair<std::string, std::string>> const& pairs, std::string_view key) {
    for (auto const& kv : pairs) {
        if (kv.first == key) return kv.second;
    }
    return {};
}

matjson::Value summarizeObject(GameObject* obj, int index, LevelEditorLayer* lel, bool includeSave = false, bool compact = false) {
    auto groupsArr = matjson::Value::array();
    for (int g : objectGroups(obj)) groupsArr.push(g);

    int const zInt = static_cast<int>(obj->m_zLayer);
    float const wy = obj->getPositionY() - kGroundOffsetY;
    int const baseCol = obj->m_baseColor ? obj->m_baseColor->m_colorID : 0;
    int const detCol = obj->m_detailColor ? obj->m_detailColor->m_colorID : 0;

    std::string rawSave;
    auto ensureRawSave = [&]() -> std::string const& {
        if (rawSave.empty() && lel) rawSave = std::string(obj->getSaveString(lel));
        return rawSave;
    };

    matjson::Value out = matjson::makeObject({});
    if (compact) {
        out = matjson::makeObject({
            {"index", index},
            {"uniqueID", obj->m_uniqueID},
            {"objectID", obj->m_objectID},
            {"x", obj->getPositionX()},
            {"worldY", wy},
        });
        if (std::abs(obj->getRotation()) > 0.001f) out["rotation"] = obj->getRotation();
        if (std::abs(obj->m_scaleX - 1.f) > 0.001f) out["scaleX"] = obj->m_scaleX;
        if (std::abs(obj->m_scaleY - 1.f) > 0.001f) out["scaleY"] = obj->m_scaleY;
        if (obj->isFlipX()) out["flipX"] = true;
        if (obj->isFlipY()) out["flipY"] = true;
        if (zInt != 0) out["zLayerName"] = zLayerName(zInt);
        if (obj->m_zOrder != 0) out["zOrder"] = obj->m_zOrder;
        if (baseCol > 0) out["baseColorID"] = baseCol;
        if (detCol > 0) out["detailColorID"] = detCol;
        if (groupsArr.size() > 0) out["groups"] = groupsArr;
        if (obj->m_isNoTouch) out["noTouch"] = true;
        if (obj->m_isHide) out["hide"] = true;
        if (obj->m_isTrigger) out["isTrigger"] = true;
    } else {
        std::string frame;
        if (auto* tb = ObjectToolbox::sharedState()) {
            if (char const* f = tb->intKeyToFrame(obj->m_objectID)) frame = f;
        }
        out = matjson::makeObject({
            {"index", index},
            {"uniqueID", obj->m_uniqueID},
            {"objectID", obj->m_objectID},
            {"frame", frame},
            {"x", obj->getPositionX()},
            {"y", obj->getPositionY()},
            {"worldY", wy},
            {"rotation", obj->getRotation()},
            {"scaleX", obj->m_scaleX},
            {"scaleY", obj->m_scaleY},
            {"flipX", obj->isFlipX()},
            {"flipY", obj->isFlipY()},
            {"zLayer", zInt},
            {"zLayerName", zLayerName(zInt)},
            {"zOrder", obj->m_zOrder},
            {"editorLayer", obj->m_editorLayer},
            {"editorLayer2", obj->m_editorLayer2},
            {"baseColorID", baseCol},
            {"detailColorID", detCol},
            {"groups", groupsArr},
            {"selected", obj->m_isSelected},
            {"isTrigger", obj->m_isTrigger},
            {"dontFade", obj->m_isDontFade},
            {"dontEnter", obj->m_isDontEnter},
            {"highDetail", obj->m_isHighDetail},
            {"hide", obj->m_isHide},
            {"noTouch", obj->m_isNoTouch},
            {"passable", obj->m_isPassable},
            {"groupParent", obj->m_hasGroupParent},
        });
    }

    if (auto* eff = typeinfo_cast<EffectGameObject*>(obj)) {
        out["targetGroupID"] = eff->m_targetGroupID;
        out["centerGroupID"] = eff->m_centerGroupID;
        out["duration"] = eff->m_duration;
        out["spawnTriggered"] = eff->m_isSpawnTriggered;
        out["touchTriggered"] = eff->m_isTouchTriggered;
        out["multiTrigger"] = eff->m_isMultiTriggered;
    }

    if (obj->m_isTrigger || obj->m_objectID == kTextObjectID) {
        auto const& save = ensureRawSave();
        if (!save.empty()) {
            auto pairs = parseSavePairs(save);
            if (obj->m_objectID == kTextObjectID) {
                auto enc = findSavePairValue(pairs, "31");
                if (!enc.empty()) out["text"] = decodeUtf8Base64(enc);
            }
            if (obj->m_isTrigger) {
                auto addIf = [&](char const* key, char const* field) {
                    auto v = findSavePairValue(pairs, key);
                    if (!v.empty()) out[field] = v;
                };
                addIf("23", "targetColorID");
                addIf("28", "moveX");
                addIf("29", "moveY");
                addIf("30", "easing");
                addIf("35", "opacity");
                addIf("56", "activateGroup");
                addIf("63", "spawnDelay");
                addIf("68", "rotateDegrees");
                addIf("77", "targetCount");
                addIf("80", "itemID");
                addIf("371", "zoom");
            }
        }
    }

    if (includeSave && lel) {
        out["saveString"] = ensureRawSave();
    }
    return out;
}

void setSavePair(std::vector<std::pair<std::string, std::string>>& pairs, std::string const& key, std::string const& val) {
    for (auto& kv : pairs) {
        if (kv.first == key) {
            kv.second = val;
            return;
        }
    }
    pairs.emplace_back(key, val);
}

std::string joinSavePairs(std::vector<std::pair<std::string, std::string>> const& pairs) {
    std::string out;
    out.reserve(pairs.size() * 8);
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) out += ',';
        out += pairs[i].first;
        out += ',';
        out += pairs[i].second;
    }
    out += ';';
    return out;
}

void applyTriggerParamsToExtra(matjson::Value const& p, matjson::Value& extra) {
    if (!p.isObject()) return;
    auto setNum = [&](char const* field, char const* key) {
        if (p.contains(field)) extra[key] = p[field];
    };
    auto setBool = [&](char const* field, char const* key) {
        if (p.contains(field)) extra[key] = p[field].asBool().unwrapOr(false) ? 1 : 0;
    };

    // Motion / Move
    setNum("move_x", "28");
    setNum("move_y", "29");
    setBool("lock_player_x", "58");
    setBool("lock_player_y", "59");
    setBool("lock_camera_x", "141");
    setBool("lock_camera_y", "142");
    setBool("small_step", "394");
    setBool("silent", "144");
    setBool("use_target_move", "100");
    setNum("target_mode", "101");

    // Color / Alpha
    setNum("target_color", "23");
    setNum("opacity", "35");
    setBool("blending", "17");
    setBool("player_color_1", "15");
    setBool("player_color_2", "16");
    setNum("copy_color_id", "50");
    setBool("copy_opacity", "60");
    if (p.contains("copy_hsv")) extra["49"] = p["copy_hsv"];
    if (p.contains("color")) {
        ccColor3B c{255, 255, 255};
        if (p["color"].isString() && parseHexColor(p["color"].asString().unwrapOr(""), c)) {
            extra["7"] = static_cast<int>(c.r);
            extra["8"] = static_cast<int>(c.g);
            extra["9"] = static_cast<int>(c.b);
        } else if (p["color"].isObject()) {
            extra["7"] = paimon::json::integerOr<int>(p["color"]["r"], 255);
            extra["8"] = paimon::json::integerOr<int>(p["color"]["g"], 255);
            extra["9"] = paimon::json::integerOr<int>(p["color"]["b"], 255);
        }
    }
    setNum("r", "7");
    setNum("g", "8");
    setNum("b", "9");

    // Pulse
    setNum("fade_in", "45");
    setNum("hold", "46");
    setNum("fade_out", "47");
    setNum("pulse_mode", "48");
    setNum("pulse_target_type", "52");
    setBool("main_only", "65");
    setBool("detail_only", "66");
    setBool("exclusive", "86");

    // Toggle / Spawn / Stop / Random / Sequence
    setBool("activate_group", "56");
    setNum("delay", "63");
    setNum("delay_variance", "556");
    if (p.contains("remap")) extra["442"] = p["remap"];
    setBool("spawn_ordered", "107");
    setBool("preview_disable", "441");
    setNum("chance", "10");
    setNum("stop_mode", "580");
    setBool("control_id", "535");
    setNum("min_int", "436");
    setNum("sequence_mode", "437");
    setNum("reset_mode", "438");
    setNum("reset_time", "439");
    if (p.contains("sequence")) extra["435"] = p["sequence"];
    if (p.contains("chance_list")) extra["152"] = p["chance_list"];

    // Rotate / Follow / Scale / Shake / Animate
    setNum("degrees", "68");
    setNum("times_360", "69");
    setBool("lock_object_rotation", "70");
    setNum("aim_target_id", "105");
    setNum("follow_x_mod", "72");
    setNum("follow_y_mod", "73");
    setNum("shake_strength", "75");
    setNum("shake_interval", "76");
    setNum("animation_id", "76");
    setNum("scale_x", "150");
    setNum("scale_y", "151");

    // Area Effects (2.2)
    setNum("area_x", "220");
    setNum("area_move_x", "220");
    setNum("area_y", "221");
    setNum("area_move_y", "221");
    setNum("area_length", "222");
    setNum("area_offset", "223");
    setNum("area_easing", "224");

    // Items / Timers / Collision
    setNum("target_count", "77");
    setNum("item_id", "80");
    setNum("block_a", "80");
    setNum("block_b", "95");
    setNum("item_id2", "95");
    setNum("comparison", "88");
    setNum("start_time", "467");
    setNum("stop_time", "468");
    setNum("time_mod", "470");
    setNum("target_time", "473");
    setNum("item1_type", "476");
    setNum("item2_type", "477");
    setNum("target_type", "478");
    setNum("mod1", "479");
    setNum("op1", "480");
    setNum("op2", "481");
    setNum("mod2", "482");
    setNum("tolerance", "483");

    // Camera / Shaders / Gameplay / Audio / Gradient / End
    setNum("zoom", "371");
    setBool("exit_static", "110");
    setBool("exit_instant", "111");
    setBool("follow_object", "212");
    setNum("follow_easing", "213");
    setNum("edge_direction", "164");
    setNum("timewarp", "120");
    setNum("gravity_mod", "148");
    setNum("min_layer", "175");
    setNum("max_layer", "176");
    setNum("song_channel", "392");
    setNum("sound_id", "392");
    setNum("song_id", "404");
    setNum("speed", "405");
    setNum("volume", "406");
    setNum("pitch", "407");
    setNum("start_ms", "408");
    setNum("end_ms", "410");
    setBool("loop", "412");
    setNum("reverb", "415");
    setNum("bpm", "498");
    setNum("beats_per_bar", "499");
    setNum("gradient_id", "174");
    setNum("up_color", "203");
    setNum("down_color", "204");
    setNum("left_color", "205");
    setNum("right_color", "206");
    setBool("vertex_mode", "207");
    setNum("blending_mode", "208");
    setNum("gradient_layer", "209");
    setBool("no_effects", "444");
    setBool("no_sfx", "445");
    setBool("instant", "446");
}

// Build a GD object save string from a structured JSON spec.
bool buildObjectSaveStringFromJson(matjson::Value const& spec, std::string& outSave, std::string& outErr) {
    if (spec.contains("raw") && spec["raw"].isString()) {
        outSave = spec["raw"].asString().unwrapOr("");
        if (outSave.empty()) {
            outErr = "Empty raw save string.";
            return false;
        }
        if (outSave.back() != ';') outSave.push_back(';');
        return true;
    }

    int objectID = paimon::json::integerOr<int>(spec["object_id"], 0);
    if (objectID <= 0) objectID = paimon::json::integerOr<int>(spec["objectID"], 0);
    if (objectID <= 0 && spec.contains("trigger") && spec["trigger"].isString()) {
        objectID = resolveTriggerId(spec["trigger"].asString().unwrapOr(""));
    }
    if (objectID <= 0) {
        outErr = "Missing or invalid object_id / trigger.";
        return false;
    }

    float const x = paimon::json::floatOr(spec["x"], 0.f);
    float const y = paimon::json::floatOr(spec["y"], 15.f);

    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.emplace_back("1", std::to_string(objectID));
    pairs.emplace_back("2", formatFloat(x));
    pairs.emplace_back("3", formatFloat(y));

    if (spec.contains("flip_x") || spec.contains("flipX")) {
        bool fx = spec.contains("flip_x") ? spec["flip_x"].asBool().unwrapOr(false) : spec["flipX"].asBool().unwrapOr(false);
        if (fx) setSavePair(pairs, "4", "1");
    }
    if (spec.contains("flip_y") || spec.contains("flipY")) {
        bool fy = spec.contains("flip_y") ? spec["flip_y"].asBool().unwrapOr(false) : spec["flipY"].asBool().unwrapOr(false);
        if (fy) setSavePair(pairs, "5", "1");
    }
    if (spec.contains("rotation")) {
        float rot = paimon::json::floatOr(spec["rotation"], 0.f);
        if (std::abs(rot) > 0.0001f) setSavePair(pairs, "6", formatFloat(rot));
    }
    if (spec.contains("scale")) {
        float sc = paimon::json::floatOr(spec["scale"], 1.f);
        setSavePair(pairs, "32", formatFloat(sc));
    }
    if (spec.contains("scale_x") || spec.contains("scaleX")) {
        float sx = spec.contains("scale_x") ? paimon::json::floatOr(spec["scale_x"], 1.f) : paimon::json::floatOr(spec["scaleX"], 1.f);
        setSavePair(pairs, "128", formatFloat(sx));
    }
    if (spec.contains("scale_y") || spec.contains("scaleY")) {
        float sy = spec.contains("scale_y") ? paimon::json::floatOr(spec["scale_y"], 1.f) : paimon::json::floatOr(spec["scaleY"], 1.f);
        setSavePair(pairs, "129", formatFloat(sy));
    }
    if (spec.contains("warp_y")) setSavePair(pairs, "131", formatFloat(paimon::json::floatOr(spec["warp_y"], 0.f)));
    if (spec.contains("warp_x")) setSavePair(pairs, "132", formatFloat(paimon::json::floatOr(spec["warp_x"], 0.f)));

    if (spec.contains("z_layer") || spec.contains("zLayer")) {
        auto const& zv = spec.contains("z_layer") ? spec["z_layer"] : spec["zLayer"];
        setSavePair(pairs, "24", std::to_string(parseZLayerValue(zv, 0)));
    }
    if (spec.contains("z_order") || spec.contains("zOrder")) {
        int zo = spec.contains("z_order") ? paimon::json::integerOr<int>(spec["z_order"], 0)
                                          : paimon::json::integerOr<int>(spec["zOrder"], 0);
        setSavePair(pairs, "25", std::to_string(zo));
    }
    if (spec.contains("editor_layer") || spec.contains("editorLayer")) {
        int el = spec.contains("editor_layer") ? paimon::json::integerOr<int>(spec["editor_layer"], 0)
                                               : paimon::json::integerOr<int>(spec["editorLayer"], 0);
        setSavePair(pairs, "20", std::to_string(std::max(0, el)));
    }
    if (spec.contains("editor_layer2") || spec.contains("editorLayer2")) {
        int el2 = spec.contains("editor_layer2") ? paimon::json::integerOr<int>(spec["editor_layer2"], 0)
                                                 : paimon::json::integerOr<int>(spec["editorLayer2"], 0);
        setSavePair(pairs, "61", std::to_string(std::max(0, el2)));
    }

    if (spec.contains("base_color") || spec.contains("baseColorID")) {
        int cid = spec.contains("base_color") ? paimon::json::integerOr<int>(spec["base_color"], 0)
                                              : paimon::json::integerOr<int>(spec["baseColorID"], 0);
        if (cid > 0) setSavePair(pairs, "21", std::to_string(cid));
    }
    if (spec.contains("detail_color") || spec.contains("detailColorID")) {
        int cid = spec.contains("detail_color") ? paimon::json::integerOr<int>(spec["detail_color"], 0)
                                                : paimon::json::integerOr<int>(spec["detailColorID"], 0);
        if (cid > 0) setSavePair(pairs, "22", std::to_string(cid));
    }
    if (spec.contains("base_hsv") && spec["base_hsv"].isString()) {
        setSavePair(pairs, "41", "1");
        setSavePair(pairs, "43", spec["base_hsv"].asString().unwrapOr(""));
    }
    if (spec.contains("detail_hsv") && spec["detail_hsv"].isString()) {
        setSavePair(pairs, "42", "1");
        setSavePair(pairs, "44", spec["detail_hsv"].asString().unwrapOr(""));
    }

    if (spec.contains("groups") && spec["groups"].isArray()) {
        std::string gs;
        for (auto const& gv : spec["groups"]) {
            int g = paimon::json::integerOr<int>(gv, 0);
            if (g > 0 && g <= 9999) {
                if (!gs.empty()) gs.push_back('.');
                gs += std::to_string(g);
            }
        }
        if (!gs.empty()) setSavePair(pairs, "57", gs);
    }

    auto setBoolKey = [&](char const* field, char const* key) {
        if (spec.contains(field) && spec[field].asBool().unwrapOr(false)) {
            setSavePair(pairs, key, "1");
        }
    };
    setBoolKey("group_parent", "34");
    setBoolKey("dont_fade", "64");
    setBoolKey("dont_enter", "67");
    setBoolKey("no_effects", "96");
    setBoolKey("high_detail", "103");
    setBoolKey("no_touch", "121");
    setBoolKey("passable", "134");
    setBoolKey("hide", "135");

    if (objectID == kTextObjectID && spec.contains("text") && spec["text"].isString()) {
        setSavePair(pairs, "31", encodeUtf8Base64(spec["text"].asString().unwrapOr("")));
    }

    // Trigger-friendly parameters (also usable on place_object if placing a trigger)
    if (spec.contains("target_group")) {
        setSavePair(pairs, "51", std::to_string(paimon::json::integerOr<int>(spec["target_group"], 0)));
    }
    if (spec.contains("center_group")) {
        setSavePair(pairs, "71", std::to_string(paimon::json::integerOr<int>(spec["center_group"], 0)));
    }
    if (spec.contains("duration")) {
        setSavePair(pairs, "10", formatFloat(paimon::json::floatOr(spec["duration"], 0.5f)));
    }
    if (spec.contains("easing")) {
        setSavePair(pairs, "30", std::to_string(parseEasingValue(spec["easing"], 0)));
    }
    if (spec.contains("easing_rate")) {
        setSavePair(pairs, "85", formatFloat(paimon::json::floatOr(spec["easing_rate"], 2.f)));
    }
    setBoolKey("touch_triggered", "11");
    setBoolKey("spawn_triggered", "62");
    setBoolKey("multi_trigger", "87");

    matjson::Value mergedExtra = (spec.contains("extra_props") && spec["extra_props"].isObject())
        ? spec["extra_props"] : matjson::makeObject({});
    if (spec.contains("params") && spec["params"].isObject()) {
        matjson::Value paramExtra = matjson::makeObject({});
        applyTriggerParamsToExtra(spec["params"], paramExtra);
        for (auto const& [k, v] : mergedExtra) {
            paramExtra[k] = v;
        }
        mergedExtra = std::move(paramExtra);
    }

    if (mergedExtra.isObject()) {
        for (auto const& [k, v] : mergedExtra) {
            if (v.isString()) setSavePair(pairs, k, v.asString().unwrapOr(""));
            else if (v.isBool()) setSavePair(pairs, k, v.asBool().unwrapOr(false) ? "1" : "0");
            else if (v.isExactlyInt()) setSavePair(pairs, k, std::to_string(v.asInt().unwrapOr(0)));
            else if (v.isNumber()) setSavePair(pairs, k, formatFloat(static_cast<float>(v.asDouble().unwrapOr(0.0))));
        }
    }

    outSave = joinSavePairs(pairs);
    return true;
}

std::vector<std::pair<GameObject*, int>> findMatchingObjects(LevelEditorLayer* lel, matjson::Value const& filter) {
    std::vector<std::pair<GameObject*, int>> out;
    if (!lel || !lel->m_objects) return out;

    std::unordered_set<int> wantIndices;
    bool hasIndices = false;
    if (filter.contains("indices") && filter["indices"].isArray()) {
        hasIndices = true;
        for (auto const& v : filter["indices"]) {
            wantIndices.insert(paimon::json::integerOr<int>(v, -1));
        }
    }

    std::unordered_set<int> wantUnique;
    bool hasUnique = false;
    if (filter.contains("unique_ids") && filter["unique_ids"].isArray()) {
        hasUnique = true;
        for (auto const& v : filter["unique_ids"]) {
            wantUnique.insert(paimon::json::integerOr<int>(v, -1));
        }
    }

    bool const selectedOnly = filter["selected_only"].asBool().unwrapOr(false);
    bool const triggersOnly = filter["triggers_only"].asBool().unwrapOr(false);
    int const wantObjectID = paimon::json::integerOr<int>(filter["object_id"], 0);
    int const wantGroupID = paimon::json::integerOr<int>(filter["group_id"], 0);
    int const wantEditorLayer = filter.contains("editor_layer") ? paimon::json::integerOr<int>(filter["editor_layer"], -1) : -1;
    int const wantColorID = paimon::json::integerOr<int>(filter["color_id"], 0);
    bool const hasZLayer = filter.contains("z_layer");
    int const wantZLayer = hasZLayer ? parseZLayerValue(filter["z_layer"], 0) : 0;

    bool const hasXMin = filter.contains("x_min");
    bool const hasXMax = filter.contains("x_max");
    bool const hasYMin = filter.contains("y_min");
    bool const hasYMax = filter.contains("y_max");
    bool const worldCoords = filter["world_coords"].asBool().unwrapOr(true);
    float const yQueryOffset = worldCoords ? kGroundOffsetY : 0.f;
    float const xMin = paimon::json::floatOr(filter["x_min"], -1e9f);
    float const xMax = paimon::json::floatOr(filter["x_max"], 1e9f);
    float const yMin = paimon::json::floatOr(filter["y_min"], -1e9f) + yQueryOffset;
    float const yMax = paimon::json::floatOr(filter["y_max"], 1e9f) + yQueryOffset;

    bool const hasBounds = filter.contains("bounds") && filter["bounds"].isObject();
    float const bMinX = hasBounds ? paimon::json::floatOr(filter["bounds"]["x_min"], -1e9f) : xMin;
    float const bMaxX = hasBounds ? paimon::json::floatOr(filter["bounds"]["x_max"], 1e9f) : xMax;
    float const bMinY = hasBounds ? (paimon::json::floatOr(filter["bounds"]["y_min"], -1e9f) + yQueryOffset) : yMin;
    float const bMaxY = hasBounds ? (paimon::json::floatOr(filter["bounds"]["y_max"], 1e9f) + yQueryOffset) : yMax;

    int const count = static_cast<int>(lel->m_objects->count());
    for (int i = 0; i < count; ++i) {
        auto* obj = typeinfo_cast<GameObject*>(lel->m_objects->objectAtIndex(i));
        if (!obj) continue;
        if (hasIndices && !wantIndices.contains(i)) continue;
        if (hasUnique && !wantUnique.contains(obj->m_uniqueID)) continue;
        if (selectedOnly && !obj->m_isSelected) continue;
        if (triggersOnly && !obj->m_isTrigger) continue;
        if (wantObjectID > 0 && obj->m_objectID != wantObjectID) continue;
        if (wantEditorLayer >= 0 && obj->m_editorLayer != wantEditorLayer) continue;
        if (hasZLayer && static_cast<int>(obj->m_zLayer) != wantZLayer) continue;
        if (wantColorID > 0) {
            int base = obj->m_baseColor ? obj->m_baseColor->m_colorID : 0;
            int det = obj->m_detailColor ? obj->m_detailColor->m_colorID : 0;
            if (base != wantColorID && det != wantColorID) continue;
        }
        if (wantGroupID > 0) {
            bool foundGroup = false;
            for (int g : objectGroups(obj)) {
                if (g == wantGroupID) { foundGroup = true; break; }
            }
            if (!foundGroup) continue;
        }
        float const ox = obj->getPositionX();
        float const oy = obj->getPositionY();
        if ((hasXMin || hasBounds) && ox < bMinX) continue;
        if ((hasXMax || hasBounds) && ox > bMaxX) continue;
        if ((hasYMin || hasBounds) && oy < bMinY) continue;
        if ((hasYMax || hasBounds) && oy > bMaxY) continue;
        out.emplace_back(obj, i);
    }
    return out;
}

void finishEditorMutation(LevelEditorLayer* lel, CCArray* updated = nullptr) {
    if (!lel) return;
    if (updated && updated->count() > 0) {
        lel->updateObjectColors(updated);
    }
    lel->recreateGroups();
    lel->dirtifyTriggers();
    lel->updateOptions();
    lel->updateLevelColors();
    lel->updateVisibility(0.f);
    if (lel->m_editorUI) {
        lel->m_editorUI->updateButtons();
    }
}

// --- Tool Implementations ---

matjson::Value toolGetEditorState() {
    auto* scene = CCDirector::get()->getRunningScene();
    auto* lel = LevelEditorLayer::get();
    if (!lel) {
        return jsonResult(matjson::makeObject({
            {"ok", true},
            {"inEditor", false},
            {"sceneActive", scene != nullptr},
            {"hint", "Not currently in LevelEditorLayer. Call list_local_levels and open_level_in_editor to open or create a level."},
        }));
    }

    auto* ui = lel->m_editorUI;
    CCSize const win = CCDirector::get()->getWinSize();
    CCPoint camCenter = lel->m_objectLayer ? lel->m_objectLayer->convertToNodeSpace(win * 0.5f) : CCPoint{0.f, 0.f};
    float const zoom = lel->m_objectLayer ? lel->m_objectLayer->getScale() : 1.f;

    int const objCount = lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0;
    int selectedCount = 0;
    float maxX = 0.f;
    std::set<int> usedGroups;
    int triggerCount = 0;
    if (lel->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            if (obj->m_isSelected) ++selectedCount;
            if (obj->m_isTrigger) ++triggerCount;
            maxX = std::max(maxX, obj->getPositionX());
            for (int g : objectGroups(obj)) usedGroups.insert(g);
        }
    }

    auto groupsArr = matjson::Value::array();
    for (int g : usedGroups) groupsArr.push(g);

    std::unordered_set<int> emptyExclude;
    int const nextFreeGroup = lel->getNextFreeGroupID(emptyExclude);
    int const nextFreeBlock = lel->getNextFreeBlockID(emptyExclude);
    int const nextFreeLayer = lel->getNextFreeEditorLayer(emptyExclude);

    auto levelJson = matjson::makeObject({});
    if (auto* lvl = lel->m_level) {
        levelJson["name"] = std::string(lvl->m_levelName);
        levelJson["levelID"] = lvl->m_levelID.value();
        levelJson["audioTrack"] = lvl->m_audioTrack;
        levelJson["songID"] = lvl->m_songID;
        levelJson["lengthUnits"] = maxX;
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"inEditor", true},
        {"level", levelJson},
        {"objectCount", objCount},
        {"triggerCount", triggerCount},
        {"selectedCount", selectedCount},
        {"usedGroups", groupsArr},
        {"nextFreeGroupID", nextFreeGroup},
        {"nextFreeBlockID", nextFreeBlock},
        {"nextFreeEditorLayer", nextFreeLayer},
        {"camera", matjson::makeObject({
            {"centerX", camCenter.x},
            {"centerY", camCenter.y - kGroundOffsetY},
            {"internalCenterY", camCenter.y},
            {"zoom", zoom},
            {"winWidth", win.width},
            {"winHeight", win.height},
        })},
        {"editorOptions", matjson::makeObject({
            {"currentLayer", lel->m_currentLayer},
            {"showGrid", lel->m_showGrid},
            {"drawTriggerBoxes", lel->m_drawTriggerBoxes},
            {"drawEffectLines", lel->m_drawEffectLines},
            {"showDurationLines", lel->m_showDurationLines},
            {"showGround", lel->m_showGround},
            {"hideBackground", lel->m_hideBackground},
            {"previewMode", lel->m_previewMode},
            {"previewParticles", lel->m_previewParticles},
            {"previewAnimations", lel->m_previewAnimations},
            {"previewShaders", lel->m_previewShaders},
            {"layerLockingEnabled", lel->m_layerLockingEnabled},
            {"selectedMode", ui ? ui->m_selectedMode : 0},
            {"swipeEnabled", ui ? ui->m_swipeEnabled : false},
            {"freeMoveEnabled", ui ? ui->m_freeMoveEnabled : false},
            {"continuousSnap", ui ? ui->m_continuousSnap : false},
        })},
    }));
}

matjson::Value toolListLocalLevels() {
    auto* llm = LocalLevelManager::sharedState();
    if (!llm || !llm->m_localLevels) return errorResult("LocalLevelManager unavailable.");

    auto levels = matjson::Value::array();
    int idx = 0;
    for (auto* lvl : CCArrayExt<GJGameLevel*>(llm->m_localLevels)) {
        if (!lvl) { ++idx; continue; }
        levels.push(matjson::makeObject({
            {"index", idx},
            {"name", std::string(lvl->m_levelName)},
            {"levelID", lvl->m_levelID.value()},
            {"objectCount", lvl->m_objectCount.value()},
            {"audioTrack", lvl->m_audioTrack},
            {"songID", lvl->m_songID},
            {"platformer", lvl->isPlatformer()},
        }));
        ++idx;
    }
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"count", levels.size()},
        {"levels", levels},
    }));
}

matjson::Value toolOpenLevelInEditor(matjson::Value const& args, std::shared_ptr<SceneSwapWaiter>& outWaiter) {
    auto* llm = LocalLevelManager::sharedState();
    auto* glm = GameLevelManager::get();
    if (!llm) return errorResult("LocalLevelManager unavailable.");

    bool const createNew = args["create_new"].asBool().unwrapOr(false);
    GJGameLevel* target = nullptr;

    if (createNew) {
        std::string name = paimon::json::stringOr(args["name"], "AI MCP Level");
        target = glm ? glm->createNewLevel() : GJGameLevel::create();
        if (!target) return errorResult("Failed to create GJGameLevel.");
        target->m_levelName = name;
        target->m_levelType = GJLevelType::Editor;
        if (args.contains("song_id")) target->m_songID = paimon::json::integerOr<int>(args["song_id"], 0);
        if (args.contains("audio_track")) target->m_audioTrack = paimon::json::integerOr<int>(args["audio_track"], 0);
        if (!glm) {
            if (!llm->m_localLevels) llm->m_localLevels = CCArray::create();
            llm->m_localLevels->insertObject(target, 0);
        }
    } else if (args.contains("index")) {
        int idx = paimon::json::integerOr<int>(args["index"], -1);
        if (!llm->m_localLevels || idx < 0 || idx >= static_cast<int>(llm->m_localLevels->count())) {
            return errorResult("Level index out of range.");
        }
        target = typeinfo_cast<GJGameLevel*>(llm->m_localLevels->objectAtIndex(idx));
    } else if (args.contains("name") && llm->m_localLevels) {
        std::string want = paimon::json::stringOr(args["name"], "");
        for (auto* lvl : CCArrayExt<GJGameLevel*>(llm->m_localLevels)) {
            if (lvl && std::string(lvl->m_levelName) == want) {
                target = lvl;
                break;
            }
        }
    }

    if (!target) return errorResult("Level not found. Provide create_new=true, a valid index, or name.");

    if (auto* currentLel = LevelEditorLayer::get()) {
        // Destroy the existing LevelEditorLayer BEFORE creating the new one so
        // ~LevelEditorLayer() cannot clear GameManager::m_levelEditorLayer after init().
        if (args["save_current"].asBool().unwrapOr(false)) {
            currentLel->saveLevel();
        }
        auto waiter = std::make_shared<SceneSwapWaiter>();
        outWaiter = waiter;
        Ref<GJGameLevel> refTarget = target;
        CCDirector::get()->replaceScene(CCScene::create());
        paimon::scheduleMainThreadDelay(0.02f, [refTarget, waiter]() {
            if (!paimon::isRuntimeShuttingDown() && !LevelEditorLayer::get()) {
                if (auto* scene = LevelEditorLayer::scene(refTarget.data(), false)) {
                    CCDirector::get()->replaceScene(scene);
                }
            }
            waiter->notifyDone();
        });
    } else {
        auto* scene = LevelEditorLayer::scene(target, false);
        if (!scene) return errorResult("Failed to create LevelEditorLayer scene.");
        CCDirector::get()->pushScene(CCTransitionFade::create(0.25f, scene));
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"opened", std::string(target->m_levelName)},
        {"created", createNew},
    }));
}

matjson::Value toolSaveLevel() {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");
    bool const saved = lel->saveLevel();
    return jsonResult(matjson::makeObject({
        {"ok", saved},
        {"levelName", lel->m_level ? std::string(lel->m_level->m_levelName) : std::string("")},
        {"objectCount", lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0},
    }), !saved);
}

matjson::Value toolGetObjects(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    auto matches = findMatchingObjects(lel, args);
    int const offset = std::max(0, paimon::json::integerOr<int>(args["offset"], 0));
    int const limit = std::clamp(paimon::json::integerOr<int>(args["limit"], 100), 1, 500);
    bool const includeSave = args["include_save_string"].asBool().unwrapOr(false);
    bool const compact = args["compact"].asBool().unwrapOr(false);

    auto items = matjson::Value::array();
    int const total = static_cast<int>(matches.size());
    for (int i = offset; i < total && static_cast<int>(items.size()) < limit; ++i) {
        items.push(summarizeObject(matches[i].first, matches[i].second, lel, includeSave, compact));
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"totalMatches", total},
        {"offset", offset},
        {"returned", items.size()},
        {"compact", compact},
        {"objects", items},
    }));
}

matjson::Value toolGetObjectRaw(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return errorResult("No active LevelEditorLayer.");

    GameObject* target = nullptr;
    int foundIdx = -1;
    int const count = static_cast<int>(lel->m_objects->count());

    if (args.contains("index")) {
        int idx = paimon::json::integerOr<int>(args["index"], -1);
        if (idx >= 0 && idx < count) {
            target = typeinfo_cast<GameObject*>(lel->m_objects->objectAtIndex(idx));
            foundIdx = idx;
        }
    } else if (args.contains("unique_id")) {
        int uid = paimon::json::integerOr<int>(args["unique_id"], -1);
        for (int i = 0; i < count; ++i) {
            auto* obj = typeinfo_cast<GameObject*>(lel->m_objects->objectAtIndex(i));
            if (obj && obj->m_uniqueID == uid) {
                target = obj;
                foundIdx = i;
                break;
            }
        }
    }
    if (!target) return errorResult("Object not found.");

    std::string const save = std::string(target->getSaveString(lel));
    auto props = matjson::makeObject({});
    for (auto const& kv : parseSavePairs(save)) {
        props[kv.first] = kv.second;
    }

    auto summary = summarizeObject(target, foundIdx, lel, true);
    summary["properties"] = props;
    return jsonResult(matjson::makeObject({{"ok", true}, {"object", summary}}));
}

matjson::Value toolPlaceObject(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    std::string saveStr;
    std::string err;
    if (!buildObjectSaveStringFromJson(args, saveStr, err)) {
        return errorResult(err);
    }

    bool const noUndo = args["no_undo"].asBool().unwrapOr(false);
    CCArray* created = lel->createObjectsFromString(saveStr, noUndo, true);
    if (!created || created->count() == 0) {
        return errorResult("GD failed to create object from generated save string.");
    }

    finishEditorMutation(lel, created);
    EditorMcpServer::get().recordObjectsCreated(created->count());

    auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(0));
    int idx = obj && lel->m_objects ? static_cast<int>(lel->m_objects->indexOfObject(obj)) : -1;
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"createdCount", static_cast<int>(created->count())},
        {"object", obj ? summarizeObject(obj, idx, lel, true) : matjson::makeObject({})},
    }));
}

matjson::Value toolPlaceObjectsBatch(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    std::string combined;
    int specCount = 0;

    if (args.contains("level_string") && args["level_string"].isString()) {
        std::string raw = args["level_string"].asString().unwrapOr("");
        if (!raw.empty()) {
            combined += raw;
            if (combined.back() != ';') combined.push_back(';');
        }
    }

    if (args.contains("objects") && args["objects"].isArray()) {
        for (auto const& item : args["objects"]) {
            if (++specCount > kMaxBatchObjects) {
                return errorResult(fmt::format("Batch limit exceeded (max {} objects per call).", kMaxBatchObjects));
            }
            std::string one;
            std::string err;
            if (!buildObjectSaveStringFromJson(item, one, err)) {
                return errorResult(fmt::format("Object #{}: {}", specCount - 1, err));
            }
            combined += one;
        }
    }

    if (combined.empty()) return errorResult("Provide non-empty 'objects' array or 'level_string'.");

    bool const noUndo = args["no_undo"].asBool().unwrapOr(false);
    CCArray* created = lel->createObjectsFromString(combined, noUndo, true);
    if (!created || created->count() == 0) {
        return errorResult("GD failed to instantiate batch objects.");
    }

    finishEditorMutation(lel, created);
    int const nCreated = static_cast<int>(created->count());
    EditorMcpServer::get().recordObjectsCreated(static_cast<uint64_t>(nCreated));

    auto createdUniqueIDs = matjson::Value::array();
    auto sample = matjson::Value::array();
    for (int i = 0; i < nCreated; ++i) {
        if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(i))) {
            createdUniqueIDs.push(obj->m_uniqueID);
            if (i < 25) {
                int idx = lel->m_objects ? static_cast<int>(lel->m_objects->indexOfObject(obj)) : -1;
                sample.push(summarizeObject(obj, idx, lel, false));
            }
        }
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"createdCount", nCreated},
        {"createdUniqueIDs", createdUniqueIDs},
        {"totalLevelObjects", lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0},
        {"sampleObjects", sample},
    }));
}

matjson::Value toolPlaceTrigger(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    int triggerID = paimon::json::integerOr<int>(args["object_id"], 0);
    if (triggerID <= 0 && args.contains("trigger") && args["trigger"].isString()) {
        triggerID = resolveTriggerId(args["trigger"].asString().unwrapOr(""));
    }
    if (triggerID <= 0) {
        return errorResult("Unknown trigger. Pass a valid trigger name (e.g. 'move', 'color', 'alpha', 'pulse', 'spawn', 'toggle', 'rotate', 'camera_zoom', 'gradient', 'glitch') or numeric object_id.");
    }

    matjson::Value spec = args;
    spec["object_id"] = triggerID;
    return toolPlaceObject(spec);
}

matjson::Value toolUpdateObjects(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    auto targets = findMatchingObjects(lel, args);
    if (targets.empty()) return errorResult("No matching objects found to update.");

    bool const needsSaveRebuild = args.contains("extra_props") || args.contains("text") ||
        args.contains("z_layer") || args.contains("zOrder") || args.contains("z_order") ||
        args.contains("base_color") || args.contains("detail_color") ||
        args.contains("base_hsv") || args.contains("detail_hsv") ||
        args.contains("dont_fade") || args.contains("dont_enter") || args.contains("no_effects") ||
        args.contains("high_detail") || args.contains("hide") ||
        args.contains("no_touch") || args.contains("passable") ||
        args.contains("group_parent") || args.contains("scale_x") || args.contains("scale_y") ||
        args.contains("scale") || args.contains("editor_layer") || args.contains("editor_layer2");

    auto* updatedArr = CCArray::create();

    if (!needsSaveRebuild) {
        // Fast in-place mutation for position/rotation/flip/groups
        for (auto const& [obj, _] : targets) {
            if (!obj) continue;
            float nx = args.contains("x") ? paimon::json::floatOr(args["x"], obj->getPositionX()) : obj->getPositionX();
            float ny = args.contains("y")
                ? (paimon::json::floatOr(args["y"], obj->getPositionY() - kGroundOffsetY) + kGroundOffsetY)
                : obj->getPositionY();
            nx += paimon::json::floatOr(args["dx"], 0.f);
            ny += paimon::json::floatOr(args["dy"], 0.f);
            if (args.contains("x") || args.contains("y") || args.contains("dx") || args.contains("dy")) {
                if (lel->m_editorUI) {
                    lel->m_editorUI->moveObject(obj, {nx - obj->getPositionX(), ny - obj->getPositionY()});
                } else {
                    obj->setPosition({nx, ny});
                    lel->updateObjectSection(obj);
                }
            }
            if (args.contains("rotation") || args.contains("d_rotation")) {
                float nr = args.contains("rotation") ? paimon::json::floatOr(args["rotation"], obj->getRotation()) : obj->getRotation();
                nr += paimon::json::floatOr(args["d_rotation"], 0.f);
                obj->setRotation(nr);
            }
            if (args.contains("flip_x")) obj->setFlipX(args["flip_x"].asBool().unwrapOr(false));
            if (args.contains("flip_y")) obj->setFlipY(args["flip_y"].asBool().unwrapOr(false));
            if (args.contains("add_groups") && args["add_groups"].isArray()) {
                for (auto const& gv : args["add_groups"]) {
                    int g = paimon::json::integerOr<int>(gv, 0);
                    if (g > 0 && g <= 9999) obj->addToGroup(g);
                }
            }
            if (args.contains("remove_groups") && args["remove_groups"].isArray()) {
                for (auto const& gv : args["remove_groups"]) {
                    int g = paimon::json::integerOr<int>(gv, 0);
                    if (g > 0) obj->removeFromGroup(g);
                }
            }
            updatedArr->addObject(obj);
        }
        finishEditorMutation(lel, updatedArr);
        return jsonResult(matjson::makeObject({
            {"ok", true},
            {"updatedCount", static_cast<int>(updatedArr->count())},
            {"rebuilt", false},
        }));
    }

    // Patch save strings and recreate cleanly so GD initializes every color, zLayer, scaleX/Y, and trigger field.
    std::string combined;
    std::vector<Ref<GameObject>> toDelete;
    for (auto const& [obj, _] : targets) {
        if (!obj) continue;
        std::string const oldSave = std::string(obj->getSaveString(lel));
        auto pairs = parseSavePairs(oldSave);

        float nx = args.contains("x") ? paimon::json::floatOr(args["x"], obj->getPositionX()) : obj->getPositionX();
        // Save-string key "3" expects world Y (ground = 0), whereas getPositionY() includes +kGroundOffsetY (90).
        float worldY = args.contains("y")
            ? paimon::json::floatOr(args["y"], obj->getPositionY() - kGroundOffsetY)
            : (obj->getPositionY() - kGroundOffsetY);
        nx += paimon::json::floatOr(args["dx"], 0.f);
        worldY += paimon::json::floatOr(args["dy"], 0.f);
        setSavePair(pairs, "2", formatFloat(nx));
        setSavePair(pairs, "3", formatFloat(worldY));

        if (args.contains("rotation") || args.contains("d_rotation")) {
            float nr = args.contains("rotation") ? paimon::json::floatOr(args["rotation"], obj->getRotation()) : obj->getRotation();
            nr += paimon::json::floatOr(args["d_rotation"], 0.f);
            setSavePair(pairs, "6", formatFloat(nr));
        }
        if (args.contains("flip_x")) setSavePair(pairs, "4", args["flip_x"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("flip_y")) setSavePair(pairs, "5", args["flip_y"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("scale")) {
            float sc = paimon::json::floatOr(args["scale"], 1.f);
            setSavePair(pairs, "32", formatFloat(sc));
            setSavePair(pairs, "128", formatFloat(sc));
            setSavePair(pairs, "129", formatFloat(sc));
        }
        if (args.contains("scale_x")) setSavePair(pairs, "128", formatFloat(paimon::json::floatOr(args["scale_x"], obj->m_scaleX)));
        if (args.contains("scale_y")) setSavePair(pairs, "129", formatFloat(paimon::json::floatOr(args["scale_y"], obj->m_scaleY)));
        if (args.contains("z_layer")) setSavePair(pairs, "24", std::to_string(parseZLayerValue(args["z_layer"], 0)));
        if (args.contains("z_order")) setSavePair(pairs, "25", std::to_string(paimon::json::integerOr<int>(args["z_order"], 0)));
        if (args.contains("editor_layer")) setSavePair(pairs, "20", std::to_string(std::max(0, paimon::json::integerOr<int>(args["editor_layer"], 0))));
        if (args.contains("editor_layer2")) setSavePair(pairs, "61", std::to_string(std::max(0, paimon::json::integerOr<int>(args["editor_layer2"], 0))));
        if (args.contains("base_color")) setSavePair(pairs, "21", std::to_string(paimon::json::integerOr<int>(args["base_color"], 0)));
        if (args.contains("detail_color")) setSavePair(pairs, "22", std::to_string(paimon::json::integerOr<int>(args["detail_color"], 0)));
        if (args.contains("base_hsv") && args["base_hsv"].isString()) {
            setSavePair(pairs, "41", "1");
            setSavePair(pairs, "43", args["base_hsv"].asString().unwrapOr(""));
        }
        if (args.contains("detail_hsv") && args["detail_hsv"].isString()) {
            setSavePair(pairs, "42", "1");
            setSavePair(pairs, "44", args["detail_hsv"].asString().unwrapOr(""));
        }
        if (args.contains("dont_fade")) setSavePair(pairs, "64", args["dont_fade"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("dont_enter")) setSavePair(pairs, "67", args["dont_enter"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("no_effects")) setSavePair(pairs, "96", args["no_effects"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("high_detail")) setSavePair(pairs, "103", args["high_detail"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("no_touch")) setSavePair(pairs, "121", args["no_touch"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("passable")) setSavePair(pairs, "134", args["passable"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("hide")) setSavePair(pairs, "135", args["hide"].asBool().unwrapOr(false) ? "1" : "0");
        if (args.contains("group_parent")) setSavePair(pairs, "34", args["group_parent"].asBool().unwrapOr(false) ? "1" : "0");
        if (obj->m_objectID == kTextObjectID && args.contains("text") && args["text"].isString()) {
            setSavePair(pairs, "31", encodeUtf8Base64(args["text"].asString().unwrapOr("")));
        }

        if (args.contains("add_groups") || args.contains("remove_groups")) {
            std::set<int> groups;
            for (int g : objectGroups(obj)) groups.insert(g);
            if (args.contains("add_groups") && args["add_groups"].isArray()) {
                for (auto const& gv : args["add_groups"]) {
                    int g = paimon::json::integerOr<int>(gv, 0);
                    if (g > 0 && g <= 9999) groups.insert(g);
                }
            }
            if (args.contains("remove_groups") && args["remove_groups"].isArray()) {
                for (auto const& gv : args["remove_groups"]) {
                    groups.erase(paimon::json::integerOr<int>(gv, 0));
                }
            }
            std::string gs;
            for (int g : groups) {
                if (!gs.empty()) gs.push_back('.');
                gs += std::to_string(g);
            }
            setSavePair(pairs, "57", gs);
        }

        if (args.contains("extra_props") && args["extra_props"].isObject()) {
            for (auto const& [k, v] : args["extra_props"]) {
                if (v.isString()) setSavePair(pairs, k, v.asString().unwrapOr(""));
                else if (v.isBool()) setSavePair(pairs, k, v.asBool().unwrapOr(false) ? "1" : "0");
                else if (v.isExactlyInt()) setSavePair(pairs, k, std::to_string(v.asInt().unwrapOr(0)));
                else if (v.isNumber()) setSavePair(pairs, k, formatFloat(static_cast<float>(v.asDouble().unwrapOr(0.0))));
            }
        }

        combined += joinSavePairs(pairs);
        toDelete.emplace_back(obj);
    }

    bool const noUndo = args["no_undo"].asBool().unwrapOr(false);
    auto* ui = lel->m_editorUI;
    for (auto& obj : toDelete) {
        if (ui) {
            if (ui->m_selectedObject == obj.data()) ui->m_selectedObject = nullptr;
            if (obj->m_isSelected || (ui->m_selectedObjects && ui->m_selectedObjects->containsObject(obj.data()))) {
                ui->deselectObject(obj.data());
            }
        }
        lel->removeObject(obj.data(), noUndo);
    }

    CCArray* created = lel->createObjectsFromString(combined, noUndo, true);
    finishEditorMutation(lel, created);

    auto newUniqueIDs = matjson::Value::array();
    if (created) {
        for (auto* obj : CCArrayExt<GameObject*>(created)) {
            if (obj) newUniqueIDs.push(obj->m_uniqueID);
        }
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"updatedCount", created ? static_cast<int>(created->count()) : 0},
        {"rebuilt", true},
        {"newUniqueIDs", newUniqueIDs},
    }));
}

matjson::Value toolDeleteObjects(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    if (args["all"].asBool().unwrapOr(false)) {
        int const prev = lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0;
        if (lel->m_editorUI) {
            lel->m_editorUI->deselectAll();
            lel->m_editorUI->m_selectedObject = nullptr;
        }
        lel->removeAllObjects();
        finishEditorMutation(lel);
        return jsonResult(matjson::makeObject({{"ok", true}, {"deletedCount", prev}}));
    }

    auto targets = findMatchingObjects(lel, args);
    if (targets.empty()) {
        return jsonResult(matjson::makeObject({{"ok", true}, {"deletedCount", 0}}));
    }

    bool const noUndo = args["no_undo"].asBool().unwrapOr(false);
    auto* ui = lel->m_editorUI;
    std::vector<Ref<GameObject>> doomed;
    doomed.reserve(targets.size());
    for (auto const& [obj, _] : targets) {
        if (obj) doomed.emplace_back(obj);
    }
    for (auto& obj : doomed) {
        if (ui) {
            if (ui->m_selectedObject == obj.data()) ui->m_selectedObject = nullptr;
            if (obj->m_isSelected || (ui->m_selectedObjects && ui->m_selectedObjects->containsObject(obj.data()))) {
                ui->deselectObject(obj.data());
            }
        }
        lel->removeObject(obj.data(), noUndo);
    }
    finishEditorMutation(lel);
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"deletedCount", static_cast<int>(doomed.size())},
        {"remainingObjects", lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0},
    }));
}

matjson::Value toolTransformObjects(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    auto* ui = lel ? lel->m_editorUI : nullptr;
    if (!lel || !ui) return errorResult("No active LevelEditorLayer/EditorUI.");

    std::string const op = paimon::json::stringOr(args["operation"], "");
    auto targets = findMatchingObjects(lel, args);
    if (targets.empty()) return errorResult("No matching objects found.");

    auto* arr = CCArray::create();
    float sumX = 0.f, sumY = 0.f;
    for (auto const& [obj, _] : targets) {
        arr->addObject(obj);
        sumX += obj->getPositionX();
        sumY += obj->getPositionY();
    }
    CCPoint pivot{sumX / targets.size(), sumY / targets.size()};
    bool const worldCoords = args["world_coords"].asBool().unwrapOr(true);
    if (args.contains("pivot_x")) {
        pivot.x = paimon::json::floatOr(args["pivot_x"], pivot.x);
    }
    if (args.contains("pivot_y")) {
        float const rawPivotY = paimon::json::floatOr(args["pivot_y"], pivot.y);
        pivot.y = worldCoords ? (rawPivotY + kGroundOffsetY) : rawPivotY;
    }

    if (op == "duplicate") {
        std::string copied = std::string(ui->copyObjects(arr, false, false));
        CCArray* pasted = ui->pasteObjects(copied, false, false);
        float const dx = paimon::json::floatOr(args["dx"], 0.f);
        float const dy = paimon::json::floatOr(args["dy"], 0.f);
        if (pasted && (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f)) {
            for (auto* obj : CCArrayExt<GameObject*>(pasted)) {
                if (obj) ui->moveObject(obj, {dx, dy});
            }
        }
        finishEditorMutation(lel, pasted);
        return jsonResult(matjson::makeObject({
            {"ok", true},
            {"operation", op},
            {"count", pasted ? static_cast<int>(pasted->count()) : 0},
        }));
    }

    if (op == "move") {
        CCPoint const delta{paimon::json::floatOr(args["dx"], 0.f), paimon::json::floatOr(args["dy"], 0.f)};
        for (auto* obj : CCArrayExt<GameObject*>(arr)) {
            ui->moveObject(obj, delta);
        }
    } else if (op == "rotate") {
        float const deg = paimon::json::floatOr(args["degrees"], 0.f);
        ui->rotateObjects(arr, deg, pivot, args["lock_rotation"].asBool().unwrapOr(false));
    } else if (op == "scale") {
        float const sx = paimon::json::floatOr(args["scale_x"], paimon::json::floatOr(args["scale"], 1.f));
        float const sy = paimon::json::floatOr(args["scale_y"], paimon::json::floatOr(args["scale"], 1.f));
        ui->scaleObjects(arr, sx, sy, pivot, ObjectScaleType::XY, false);
    } else if (op == "flip_x") {
        ui->flipObjectsX(arr);
    } else if (op == "flip_y") {
        ui->flipObjectsY(arr);
    } else {
        return errorResult("Unknown operation. Use: move, rotate, scale, flip_x, flip_y, duplicate.");
    }

    finishEditorMutation(lel, arr);
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"operation", op},
        {"count", static_cast<int>(arr->count())},
    }));
}

matjson::Value toolSelectObjects(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    auto* ui = lel ? lel->m_editorUI : nullptr;
    if (!lel || !ui) return errorResult("No active LevelEditorLayer/EditorUI.");

    if (args["none"].asBool().unwrapOr(false)) {
        ui->deselectAll();
        return jsonResult(matjson::makeObject({{"ok", true}, {"selectedCount", 0}}));
    }

    bool const additive = args["additive"].asBool().unwrapOr(false);
    if (!additive) ui->deselectAll();

    matjson::Value filter = args;
    filter["selected_only"] = false;
    auto matches = findMatchingObjects(lel, filter);
    auto* arr = CCArray::create();
    float sumX = 0.f, sumY = 0.f;
    for (auto const& [obj, _] : matches) {
        arr->addObject(obj);
        sumX += obj->getPositionX();
        sumY += obj->getPositionY();
    }
    if (arr->count() > 0) {
        if (arr->count() == 1) {
            if (auto* single = typeinfo_cast<GameObject*>(arr->objectAtIndex(0))) {
                ui->selectObject(single, true);
            }
        } else {
            ui->selectObjects(arr, true);
        }
        if (args["focus_camera"].asBool().unwrapOr(false) && lel->m_objectLayer) {
            CCPoint const center{sumX / arr->count(), sumY / arr->count()};
            CCSize const win = CCDirector::get()->getWinSize();
            CCPoint const world = lel->m_objectLayer->convertToWorldSpace(center);
            lel->m_objectLayer->setPosition(lel->m_objectLayer->getPosition() + win * 0.5f - world);
            ui->constrainGameLayerPosition();
            lel->updateVisibility(0.f);
        }
    }
    ui->updateButtons();
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"selectedCount", static_cast<int>(arr->count())},
    }));
}

matjson::Value toolUndoRedo(bool isRedo) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");
    if (isRedo) lel->redoLastAction();
    else lel->undoLastAction();
    finishEditorMutation(lel);
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"action", isRedo ? "redo" : "undo"},
        {"objectCount", lel->m_objects ? static_cast<int>(lel->m_objects->count()) : 0},
    }));
}

matjson::Value toolManageGroups(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    std::string const action = paimon::json::stringOr(args["action"], "summary");
    if (action == "allocate") {
        int const count = std::clamp(paimon::json::integerOr<int>(args["count"], 1), 1, 64);
        std::string const kind = paimon::json::stringOr(args["kind"], "group");
        std::unordered_set<int> excluded;
        auto ids = matjson::Value::array();
        for (int i = 0; i < count; ++i) {
            int id = 0;
            if (kind == "block") id = lel->getNextFreeBlockID(excluded);
            else if (kind == "layer") id = lel->getNextFreeEditorLayer(excluded);
            else id = lel->getNextFreeGroupID(excluded);
            if (id <= 0) break;
            excluded.insert(id);
            ids.push(id);
        }
        return jsonResult(matjson::makeObject({{"ok", true}, {"kind", kind}, {"ids", ids}}));
    }

    if (action == "add" || action == "remove") {
        int const groupID = paimon::json::integerOr<int>(args["group_id"], 0);
        if (groupID <= 0 || groupID > 9999) return errorResult("Invalid group_id (1..9999).");
        auto targets = findMatchingObjects(lel, args);
        int affected = 0;
        for (auto const& [obj, _] : targets) {
            if (!obj) continue;
            if (action == "add") obj->addToGroup(groupID);
            else obj->removeFromGroup(groupID);
            ++affected;
        }
        finishEditorMutation(lel);
        return jsonResult(matjson::makeObject({
            {"ok", true}, {"action", action}, {"groupID", groupID}, {"affected", affected},
        }));
    }

    if (action == "set_parent") {
        int const groupID = paimon::json::integerOr<int>(args["group_id"], 0);
        if (groupID <= 0) return errorResult("Invalid group_id.");
        auto targets = findMatchingObjects(lel, args);
        if (targets.empty()) return errorResult("No matching object found to set as group parent.");
        lel->setGroupParent(targets.front().first, groupID);
        finishEditorMutation(lel);
        return jsonResult(matjson::makeObject({
            {"ok", true}, {"action", "set_parent"}, {"groupID", groupID},
            {"uniqueID", targets.front().first->m_uniqueID},
        }));
    }

    // Default: summary of all used groups and counts
    std::map<int, int> groupCounts;
    if (lel->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            for (int g : objectGroups(obj)) ++groupCounts[g];
        }
    }
    auto groupsJson = matjson::Value::array();
    for (auto const& [gid, cnt] : groupCounts) {
        groupsJson.push(matjson::makeObject({{"groupID", gid}, {"objectCount", cnt}}));
    }
    std::unordered_set<int> emptyEx;
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"nextFreeGroupID", lel->getNextFreeGroupID(emptyEx)},
        {"nextFreeBlockID", lel->getNextFreeBlockID(emptyEx)},
        {"nextFreeEditorLayer", lel->getNextFreeEditorLayer(emptyEx)},
        {"groups", groupsJson},
    }));
}

matjson::Value colorActionToJson(int id, ColorAction* ca) {
    if (!ca) return matjson::makeObject({{"channelID", id}, {"exists", false}});
    return matjson::makeObject({
        {"channelID", id},
        {"exists", true},
        {"r", static_cast<int>(ca->m_fromColor.r)},
        {"g", static_cast<int>(ca->m_fromColor.g)},
        {"b", static_cast<int>(ca->m_fromColor.b)},
        {"hex", fmt::format("#{:02x}{:02x}{:02x}", ca->m_fromColor.r, ca->m_fromColor.g, ca->m_fromColor.b)},
        {"opacity", ca->m_fromOpacity},
        {"blending", ca->m_blending},
        {"playerColor", ca->m_playerColor},
        {"copyID", ca->m_copyID},
        {"copyOpacity", ca->m_copyOpacity},
    });
}

matjson::Value toolGetColorChannels(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    auto* em = getEffectManager(lel);
    if (!lel || !em) return errorResult("No active LevelEditorLayer/GJEffectManager.");

    if (args.contains("channel_id")) {
        int cid = paimon::json::integerOr<int>(args["channel_id"], 1);
        return jsonResult(matjson::makeObject({
            {"ok", true},
            {"channel", colorActionToJson(cid, em->getColorAction(cid))},
        }));
    }

    auto channels = matjson::Value::array();
    std::vector<int> ids;
    int const maxCustom = std::clamp(paimon::json::integerOr<int>(args["max_custom"], 50), 1, 999);
    for (int id : {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1009, 1010, 1011, 1012}) {
        ids.push_back(id);
    }
    for (int id = 1; id <= maxCustom; ++id) ids.push_back(id);

    bool const onlyUsed = args["only_active"].asBool().unwrapOr(true);
    for (int id : ids) {
        auto* ca = em->getColorAction(id);
        if (!ca) continue;
        if (onlyUsed && id < 1000 && !ca->isInUse() &&
            ca->m_fromColor.r == 255 && ca->m_fromColor.g == 255 && ca->m_fromColor.b == 255 &&
            !ca->m_blending && ca->m_fromOpacity >= 0.999f && ca->m_copyID == 0) {
            continue;
        }
        channels.push(colorActionToJson(id, ca));
    }
    return jsonResult(matjson::makeObject({{"ok", true}, {"channels", channels}}));
}

bool applySingleColorChannel(
    GJEffectManager* em,
    matjson::Value const& spec,
    matjson::Value& outChannel,
    std::string& outErr
) {
    int const cid = paimon::json::integerOr<int>(spec["channel_id"], 0);
    if (cid <= 0 || cid > 1012) {
        outErr = "Invalid channel_id (1..999 or 1000..1012).";
        return false;
    }

    ColorAction* ca = em->getColorAction(cid);
    ccColor3B col = ca ? ca->m_fromColor : ccColor3B{255, 255, 255};
    if (spec.contains("hex") && spec["hex"].isString()) {
        if (!parseHexColor(spec["hex"].asString().unwrapOr(""), col)) {
            outErr = fmt::format("Channel {}: invalid hex color (expected #RRGGBB).", cid);
            return false;
        }
    }
    if (spec.contains("r")) col.r = static_cast<GLubyte>(std::clamp(paimon::json::integerOr<int>(spec["r"], col.r), 0, 255));
    if (spec.contains("g")) col.g = static_cast<GLubyte>(std::clamp(paimon::json::integerOr<int>(spec["g"], col.g), 0, 255));
    if (spec.contains("b")) col.b = static_cast<GLubyte>(std::clamp(paimon::json::integerOr<int>(spec["b"], col.b), 0, 255));

    float const opacity = std::clamp(
        paimon::json::floatOr(spec["opacity"], ca ? ca->m_fromOpacity : 1.f), 0.f, 1.f);
    bool const blending = spec.contains("blending")
        ? spec["blending"].asBool().unwrapOr(false)
        : (ca ? ca->m_blending : false);
    int const playerColor = spec.contains("player_color")
        ? paimon::json::integerOr<int>(spec["player_color"], 0)
        : (ca ? ca->m_playerColor : 0);

    if (!ca) {
        ca = ColorAction::create(col, col, 0.f, 0.0, blending, playerColor, opacity, opacity);
        ca->m_colorID = cid;
        em->setColorAction(ca, cid);
    } else {
        ca->m_fromColor = col;
        ca->m_toColor = col;
        ca->m_color = col;
        ca->m_fromOpacity = opacity;
        ca->m_toOpacity = opacity;
        ca->m_currentOpacity = opacity;
        ca->m_blending = blending;
        ca->m_playerColor = playerColor;
    }

    if (spec.contains("copy_color_id")) ca->m_copyID = paimon::json::integerOr<int>(spec["copy_color_id"], 0);
    if (spec.contains("copy_opacity")) ca->m_copyOpacity = spec["copy_opacity"].asBool().unwrapOr(false);

    em->updateColorAction(ca);
    outChannel = colorActionToJson(cid, ca);
    return true;
}

matjson::Value toolSetColorChannel(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    auto* em = getEffectManager(lel);
    if (!lel || !em) return errorResult("No active LevelEditorLayer/GJEffectManager.");

    if (args.contains("channels") && args["channels"].isArray()) {
        auto updated = matjson::Value::array();
        int idx = 0;
        for (auto const& item : args["channels"]) {
            matjson::Value chJson;
            std::string err;
            if (!applySingleColorChannel(em, item, chJson, err)) {
                return errorResult(fmt::format("channels[{}]: {}", idx, err));
            }
            updated.push(std::move(chJson));
            ++idx;
        }
        lel->m_updateColorSprites = true;
        lel->updateLevelColors();
        if (lel->m_objects) lel->updateObjectColors(lel->m_objects);
        return jsonResult(matjson::makeObject({
            {"ok", true},
            {"updatedCount", updated.size()},
            {"channels", updated},
        }));
    }

    matjson::Value chJson;
    std::string err;
    if (!applySingleColorChannel(em, args, chJson, err)) {
        return errorResult(err);
    }

    lel->m_updateColorSprites = true;
    lel->updateLevelColors();
    if (lel->m_objects) lel->updateObjectColors(lel->m_objects);

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"channel", chJson},
    }));
}

matjson::Value toolGetLevelSettings() {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_levelSettings) return errorResult("No active LevelEditorLayer/LevelSettingsObject.");

    auto* s = lel->m_levelSettings;
    auto out = matjson::makeObject({
        {"ok", true},
        {"startMode", s->m_startMode},
        {"startSpeed", static_cast<int>(s->m_startSpeed)},
        {"startMini", s->m_startMini},
        {"startDual", s->m_startDual},
        {"twoPlayerMode", s->m_twoPlayerMode},
        {"platformerMode", s->m_platformerMode},
        {"mirrorMode", s->m_mirrorMode},
        {"rotateGameplay", s->m_rotateGameplay},
        {"reverseGameplay", s->m_reverseGameplay},
        {"isFlipped", s->m_isFlipped},
        {"backgroundIndex", s->m_backgroundIndex},
        {"groundIndex", s->m_groundIndex},
        {"middleGroundIndex", s->m_middleGroundIndex},
        {"groundLineIndex", s->m_groundLineIndex},
        {"fontIndex", s->m_fontIndex},
        {"songOffset", s->m_songOffset},
        {"fadeIn", s->m_fadeIn},
        {"fadeOut", s->m_fadeOut},
        {"dontReset", s->m_dontReset},
        {"spawnGroup", s->m_spawnGroup},
        {"allowMultiRotation", s->m_allowMultiRotation},
        {"enable22Changes", s->m_enable22Changes},
        {"fixGravityBug", s->m_fixGravityBug},
        {"fixNegativeScale", s->m_fixNegativeScale},
        {"fixRobotJump", s->m_fixRobotJump},
        {"saveString", std::string(s->getSaveString())},
    });
    if (auto* lvl = lel->m_level) {
        out["levelName"] = std::string(lvl->m_levelName);
        out["audioTrack"] = lvl->m_audioTrack;
        out["songID"] = lvl->m_songID;
    }
    return jsonResult(out);
}

matjson::Value toolSetLevelSettings(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_levelSettings) return errorResult("No active LevelEditorLayer/LevelSettingsObject.");
    auto* s = lel->m_levelSettings;

    if (args.contains("start_mode")) s->m_startMode = std::clamp(paimon::json::integerOr<int>(args["start_mode"], s->m_startMode), 0, 7);
    if (args.contains("start_speed")) s->m_startSpeed = static_cast<Speed>(std::clamp(paimon::json::integerOr<int>(args["start_speed"], 0), 0, 4));
    if (args.contains("start_mini")) s->m_startMini = args["start_mini"].asBool().unwrapOr(s->m_startMini);
    if (args.contains("start_dual")) s->m_startDual = args["start_dual"].asBool().unwrapOr(s->m_startDual);
    if (args.contains("two_player_mode")) s->m_twoPlayerMode = args["two_player_mode"].asBool().unwrapOr(s->m_twoPlayerMode);
    if (args.contains("platformer_mode")) s->m_platformerMode = args["platformer_mode"].asBool().unwrapOr(s->m_platformerMode);
    if (args.contains("mirror_mode")) s->m_mirrorMode = args["mirror_mode"].asBool().unwrapOr(s->m_mirrorMode);
    if (args.contains("rotate_gameplay")) s->m_rotateGameplay = args["rotate_gameplay"].asBool().unwrapOr(s->m_rotateGameplay);
    if (args.contains("reverse_gameplay")) s->m_reverseGameplay = args["reverse_gameplay"].asBool().unwrapOr(s->m_reverseGameplay);
    if (args.contains("background_index")) s->m_backgroundIndex = std::max(0, paimon::json::integerOr<int>(args["background_index"], s->m_backgroundIndex));
    if (args.contains("ground_index")) s->m_groundIndex = std::max(0, paimon::json::integerOr<int>(args["ground_index"], s->m_groundIndex));
    if (args.contains("middleground_index")) s->m_middleGroundIndex = std::max(0, paimon::json::integerOr<int>(args["middleground_index"], s->m_middleGroundIndex));
    if (args.contains("ground_line_index")) s->m_groundLineIndex = std::max(0, paimon::json::integerOr<int>(args["ground_line_index"], s->m_groundLineIndex));
    if (args.contains("font_index")) s->m_fontIndex = std::max(0, paimon::json::integerOr<int>(args["font_index"], s->m_fontIndex));
    if (args.contains("song_offset")) s->m_songOffset = paimon::json::floatOr(args["song_offset"], s->m_songOffset);
    if (args.contains("spawn_group")) s->m_spawnGroup = paimon::json::integerOr<int>(args["spawn_group"], s->m_spawnGroup);
    if (args.contains("enable_22_changes")) s->m_enable22Changes = args["enable_22_changes"].asBool().unwrapOr(s->m_enable22Changes);

    if (auto* lvl = lel->m_level) {
        if (args.contains("level_name") && args["level_name"].isString()) {
            lvl->m_levelName = args["level_name"].asString().unwrapOr(std::string(lvl->m_levelName));
        }
        if (args.contains("audio_track")) lvl->m_audioTrack = std::max(0, paimon::json::integerOr<int>(args["audio_track"], lvl->m_audioTrack));
        if (args.contains("song_id")) lvl->m_songID = std::max(0, paimon::json::integerOr<int>(args["song_id"], lvl->m_songID));
    }

    lel->updateOptions();
    lel->updateLevelColors();
    return toolGetLevelSettings();
}

matjson::Value toolSetEditorCameraAndOptions(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");
    auto* ui = lel->m_editorUI;

    if (args.contains("zoom") && ui) {
        float const z = std::clamp(paimon::json::floatOr(args["zoom"], 1.f), 0.1f, 4.0f);
        ui->updateZoom(z);
    }
    if ((args.contains("x") || args.contains("y")) && lel->m_objectLayer) {
        CCSize const win = CCDirector::get()->getWinSize();
        CCPoint const cur = lel->m_objectLayer->convertToNodeSpace(win * 0.5f);
        bool const worldCoords = args["world_coords"].asBool().unwrapOr(true);
        float targetY = cur.y;
        if (args.contains("y")) {
            float const rawY = paimon::json::floatOr(args["y"], cur.y - (worldCoords ? kGroundOffsetY : 0.f));
            targetY = worldCoords ? (rawY + kGroundOffsetY) : rawY;
        }
        CCPoint const target{
            args.contains("x") ? paimon::json::floatOr(args["x"], cur.x) : cur.x,
            targetY,
        };
        CCPoint const world = lel->m_objectLayer->convertToWorldSpace(target);
        lel->m_objectLayer->setPosition(lel->m_objectLayer->getPosition() + win * 0.5f - world);
        if (ui) ui->constrainGameLayerPosition();
    }

    if (args.contains("current_layer")) {
        lel->m_currentLayer = static_cast<short>(std::clamp(paimon::json::integerOr<int>(args["current_layer"], -1), -1, 999));
    }
    if (args.contains("show_grid")) lel->m_showGrid = args["show_grid"].asBool().unwrapOr(lel->m_showGrid);
    if (args.contains("draw_trigger_boxes")) lel->m_drawTriggerBoxes = args["draw_trigger_boxes"].asBool().unwrapOr(lel->m_drawTriggerBoxes);
    if (args.contains("draw_effect_lines")) lel->m_drawEffectLines = args["draw_effect_lines"].asBool().unwrapOr(lel->m_drawEffectLines);
    if (args.contains("show_duration_lines")) lel->m_showDurationLines = args["show_duration_lines"].asBool().unwrapOr(lel->m_showDurationLines);
    if (args.contains("show_ground")) lel->m_showGround = args["show_ground"].asBool().unwrapOr(lel->m_showGround);
    if (args.contains("hide_background")) lel->m_hideBackground = args["hide_background"].asBool().unwrapOr(lel->m_hideBackground);
    if (args.contains("preview_mode")) lel->m_previewMode = args["preview_mode"].asBool().unwrapOr(lel->m_previewMode);
    if (args.contains("preview_particles")) lel->m_previewParticles = args["preview_particles"].asBool().unwrapOr(lel->m_previewParticles);
    if (args.contains("preview_animations")) lel->m_previewAnimations = args["preview_animations"].asBool().unwrapOr(lel->m_previewAnimations);
    if (args.contains("preview_shaders")) lel->m_previewShaders = args["preview_shaders"].asBool().unwrapOr(lel->m_previewShaders);
    if (args.contains("layer_locking")) lel->m_layerLockingEnabled = args["layer_locking"].asBool().unwrapOr(lel->m_layerLockingEnabled);

    if (ui) {
        if (args.contains("swipe")) ui->m_swipeEnabled = args["swipe"].asBool().unwrapOr(ui->m_swipeEnabled);
        if (args.contains("free_move")) ui->m_freeMoveEnabled = args["free_move"].asBool().unwrapOr(ui->m_freeMoveEnabled);
        if (args.contains("snap")) ui->m_continuousSnap = args["snap"].asBool().unwrapOr(ui->m_continuousSnap);
        if (args.contains("mode")) ui->m_selectedMode = std::clamp(paimon::json::integerOr<int>(args["mode"], ui->m_selectedMode), 1, 4);
    }

    finishEditorMutation(lel);
    return toolGetEditorState();
}

RenderRequest parseRenderRequest(matjson::Value const& args) {
    RenderRequest req;
    req.mode = paimon::json::stringOr(args["mode"], "1to1");
    req.worldCoords = args["world_coords"].asBool().unwrapOr(true);
    if (args.contains("x") || args.contains("y")) {
        req.hasCenter = true;
        req.centerX = paimon::json::floatOr(args["x"], 0.f);
        req.centerY = paimon::json::floatOr(args["y"], 105.f);
    }
    if (args.contains("zoom")) req.zoom = paimon::json::floatOr(args["zoom"], 1.f);
    if (args.contains("width")) req.width = paimon::json::integerOr<int>(args["width"], 1280);
    if (args.contains("height")) req.height = paimon::json::integerOr<int>(args["height"], 720);
    req.showGrid = args["show_grid"].asBool().unwrapOr(false);
    req.showCoords = args.contains("show_coords") ? args["show_coords"].asBool().unwrapOr(false) : req.showGrid;
    req.showHitboxes = args["show_hitboxes"].asBool().unwrapOr(false);
    req.highlightGroup = paimon::json::integerOr<int>(args["highlight_group"], 0);
    req.showUI = args["show_ui"].asBool().unwrapOr(false);
    req.showTriggerBoxes = args["show_trigger_boxes"].asBool().unwrapOr(false);
    req.previewColors = args["preview_colors"].asBool().unwrapOr(true);
    if (args.contains("bounds") && args["bounds"].isObject()) {
        req.hasBounds = true;
        req.boundsMinX = paimon::json::floatOr(args["bounds"]["x_min"], 0.f);
        req.boundsMaxX = paimon::json::floatOr(args["bounds"]["x_max"], 480.f);
        req.boundsMinY = paimon::json::floatOr(args["bounds"]["y_min"], 0.f);
        req.boundsMaxY = paimon::json::floatOr(args["bounds"]["y_max"], 300.f);
    }
    req.savePath = paimon::json::stringOr(args["save_path"], "");
    return req;
}

matjson::Value toolRender1To1(matjson::Value const& args) {
    RenderRequest const req = parseRenderRequest(args);
    RenderResult const rr = renderEditor1To1(req);
    if (!rr.ok) return errorResult(rr.error);

    EditorMcpServer::get().recordRenderServed();

    auto meta = matjson::makeObject({
        {"ok", true},
        {"mode", req.mode},
        {"width", rr.width},
        {"height", rr.height},
        {"centerX", rr.centerX},
        {"centerY", rr.centerY},
        {"zoom", rr.zoom},
        {"pixelsPerGDUnit", rr.pixelsPerUnit},
        {"worldBounds", matjson::makeObject({
            {"minX", rr.worldMinX},
            {"maxX", rr.worldMaxX},
            {"minY", rr.worldMinY},
            {"maxY", rr.worldMaxY},
        })},
        {"visibleObjectCount", rr.visibleObjectCount},
        {"totalObjectCount", rr.totalObjectCount},
    });
    if (!rr.savedPath.empty()) meta["savedPath"] = rr.savedPath;

    return matjson::makeObject({
        {"content", jsonArray({
            matjson::makeObject({
                {"type", "image"},
                {"data", rr.pngBase64},
                {"mimeType", "image/png"},
            }),
            matjson::makeObject({
                {"type", "text"},
                {"text", meta.dump(matjson::NO_INDENTATION)},
            }),
        })},
        {"isError", false},
    });
}

// --- Geometry Classification & Quality Helpers ---

enum class ObjectRole { SolidBlock, Slope, Hazard, OrbOrPad, Portal, Trigger, Deco };

bool isSlopeObjectID(int id) {
    switch (id) {
        case 289: case 290: case 291: case 292: case 294: case 295:
        case 695: case 696: case 1743: case 1744: case 1745: case 1746:
        case 1747: case 1748: case 1749: case 1750:
            return true;
        default:
            return false;
    }
}

bool isHazardObjectID(int id) {
    switch (id) {
        case 8: case 9: case 39: case 103: case 144: case 145: case 216:
        case 88: case 89: case 98: case 1705: case 1706: case 1707:
            return true;
        default:
            return false;
    }
}

bool isOrbOrPadObjectID(int id) {
    switch (id) {
        case 35: case 36: case 67: case 84: case 140: case 141:
        case 1022: case 1330: case 1332: case 1333: case 1704: case 1751:
        case 2866: case 3004: case 3027:
            return true;
        default:
            return false;
    }
}

bool isPortalObjectID(int id) {
    switch (id) {
        case 10: case 11: case 12: case 13: case 45: case 46: case 47:
        case 99: case 101: case 111: case 200: case 201: case 202: case 203:
        case 286: case 287: case 660: case 745: case 747: case 749:
        case 1331: case 1334: case 1933:
            return true;
        default:
            return false;
    }
}

bool isSolidBlockObjectID(int id) {
    if ((id >= 1 && id <= 7) || (id >= 467 && id <= 470) ||
        (id >= 69 && id <= 75) || (id >= 90 && id <= 96) || (id >= 160 && id <= 169)) {
        return true;
    }
    return id == 211;
}

ObjectRole classifyObjectRole(GameObject* obj) {
    if (!obj) return ObjectRole::Deco;
    if (obj->m_isTrigger) return ObjectRole::Trigger;
    int const id = obj->m_objectID;
    if (isHazardObjectID(id) && !obj->m_isNoTouch) return ObjectRole::Hazard;
    if (isSlopeObjectID(id) && !obj->m_isNoTouch) return ObjectRole::Slope;
    if (isOrbOrPadObjectID(id)) return ObjectRole::OrbOrPad;
    if (isPortalObjectID(id)) return ObjectRole::Portal;
    if (isSolidBlockObjectID(id) && !obj->m_isNoTouch) return ObjectRole::SolidBlock;
    return ObjectRole::Deco;
}

struct WorldBounds {
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    bool valid = false;

    void expand(float x, float y, float halfW = 0.f, float halfH = 0.f) {
        minX = std::min(minX, x - halfW);
        maxX = std::max(maxX, x + halfW);
        minY = std::min(minY, y - halfH);
        maxY = std::max(maxY, y + halfH);
        valid = true;
    }

    matjson::Value toJson() const {
        if (!valid) return matjson::makeObject({{"valid", false}});
        return matjson::makeObject({
            {"valid", true},
            {"x_min", minX},
            {"x_max", maxX},
            {"y_min", minY},
            {"y_max", maxY},
            {"width", maxX - minX},
            {"height", maxY - minY},
            {"centerX", (minX + maxX) * 0.5f},
            {"centerY", (minY + maxY) * 0.5f},
        });
    }
};

// Note: args bounds/y_min/y_max are accepted in world Y (ground = 0) for geometry analysis tools.
std::vector<std::pair<GameObject*, int>> findObjectsWorldFilter(LevelEditorLayer* lel, matjson::Value const& args) {
    matjson::Value adjusted = args;
    adjusted["world_coords"] = true;
    return findMatchingObjects(lel, adjusted);
}

matjson::Value toolInspectStructureBounds(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    auto matches = findObjectsWorldFilter(lel, args);
    bool const excludeTriggers = args["exclude_triggers"].asBool().unwrapOr(true);

    WorldBounds solidBounds;
    WorldBounds allBounds;

    struct BlockCell { int gx; int gy; float x; float y; GameObject* obj; };
    std::vector<BlockCell> unitBlocks;
    std::set<std::pair<int, int>> occupiedCells;

    auto blocksArr = matjson::Value::array();
    auto slopesArr = matjson::Value::array();
    auto hazardsArr = matjson::Value::array();
    auto interactivesArr = matjson::Value::array();
    int decoCount = 0;

    for (auto const& [obj, idx] : matches) {
        if (!obj) continue;
        ObjectRole const role = classifyObjectRole(obj);
        if (excludeTriggers && role == ObjectRole::Trigger) continue;

        float const wx = obj->getPositionX();
        float const wy = obj->getPositionY() - kGroundOffsetY;
        float const hw = 15.f * std::abs(obj->m_scaleX);
        float const hh = 15.f * std::abs(obj->m_scaleY);
        allBounds.expand(wx, wy, hw, hh);

        if (role == ObjectRole::SolidBlock) {
            solidBounds.expand(wx, wy, hw, hh);
            int const gx = static_cast<int>(std::llround((wx - 15.f) / kBlockSizeUnits));
            int const gy = static_cast<int>(std::llround((wy - 15.f) / kBlockSizeUnits));
            bool const isUnitGrid = std::abs((wx - 15.f) - gx * kBlockSizeUnits) < 0.5f &&
                                    std::abs((wy - 15.f) - gy * kBlockSizeUnits) < 0.5f &&
                                    std::abs(obj->m_scaleX - 1.f) < 0.05f &&
                                    std::abs(obj->m_scaleY - 1.f) < 0.05f;
            if (isUnitGrid) {
                occupiedCells.insert({gx, gy});
                unitBlocks.push_back({gx, gy, wx, wy, obj});
            } else {
                blocksArr.push(matjson::makeObject({
                    {"uniqueID", obj->m_uniqueID},
                    {"objectID", obj->m_objectID},
                    {"x", wx}, {"worldY", wy},
                    {"scaleX", obj->m_scaleX}, {"scaleY", obj->m_scaleY},
                    {"hide", obj->m_isHide},
                }));
            }
        } else if (role == ObjectRole::Slope) {
            bool const is2x1 = (obj->m_objectID == 290 || obj->m_objectID == 292 ||
                                obj->m_objectID == 696 || obj->m_objectID == 1744 ||
                                obj->m_objectID == 1746 || obj->m_objectID == 1748 ||
                                obj->m_objectID == 1750);
            float const dx = is2x1 ? 60.f : 30.f;
            float const dy = 30.f;
            float const rot = obj->getRotation();
            bool const vertical2x1 = is2x1 && (std::abs(std::fmod(std::abs(rot), 180.f) - 90.f) < 1.f);
            float const spanW = (vertical2x1 ? dy : dx) * std::abs(obj->m_scaleX);
            float const spanH = (vertical2x1 ? dx : dy) * std::abs(obj->m_scaleY);
            solidBounds.expand(wx, wy, spanW * 0.5f, spanH * 0.5f);
            float const hypBlocks = std::sqrt(spanW * spanW + spanH * spanH) / kBlockSizeUnits;
            float const angleDeg = std::atan2(spanH, spanW) * (180.f / 3.14159265f);
            slopesArr.push(matjson::makeObject({
                {"uniqueID", obj->m_uniqueID},
                {"objectID", obj->m_objectID},
                {"x", wx}, {"worldY", wy},
                {"rotation", rot},
                {"flipX", obj->isFlipX()}, {"flipY", obj->isFlipY()},
                {"spanWidth", spanW}, {"spanHeight", spanH},
                {"hypotenuseBlocks", hypBlocks},
                {"slopeAngleDeg", angleDeg},
                {"hide", obj->m_isHide},
            }));
        } else if (role == ObjectRole::Hazard) {
            solidBounds.expand(wx, wy, hw, hh);
            hazardsArr.push(matjson::makeObject({
                {"uniqueID", obj->m_uniqueID},
                {"objectID", obj->m_objectID},
                {"x", wx}, {"worldY", wy},
                {"rotation", obj->getRotation()},
                {"scaleX", obj->m_scaleX}, {"scaleY", obj->m_scaleY},
                {"hide", obj->m_isHide},
            }));
        } else if (role == ObjectRole::OrbOrPad || role == ObjectRole::Portal) {
            interactivesArr.push(matjson::makeObject({
                {"uniqueID", obj->m_uniqueID},
                {"objectID", obj->m_objectID},
                {"role", role == ObjectRole::Portal ? "portal" : "orb_or_pad"},
                {"x", wx}, {"worldY", wy},
                {"rotation", obj->getRotation()},
            }));
        } else {
            ++decoCount;
        }
    }

    for (auto const& c : unitBlocks) {
        bool const openTop = !occupiedCells.contains({c.gx, c.gy + 1});
        bool const openBottom = !occupiedCells.contains({c.gx, c.gy - 1}) && c.gy > 0;
        bool const openLeft = !occupiedCells.contains({c.gx - 1, c.gy});
        bool const openRight = !occupiedCells.contains({c.gx + 1, c.gy});
        blocksArr.push(matjson::makeObject({
            {"uniqueID", c.obj->m_uniqueID},
            {"objectID", c.obj->m_objectID},
            {"x", c.x}, {"worldY", c.y},
            {"gridX", c.gx}, {"gridY", c.gy},
            {"openTop", openTop}, {"openBottom", openBottom},
            {"openLeft", openLeft}, {"openRight", openRight},
            {"hide", c.obj->m_isHide},
        }));
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"solidBoundingBox", solidBounds.toJson()},
        {"allObjectsBoundingBox", allBounds.toJson()},
        {"counts", matjson::makeObject({
            {"blocks", blocksArr.size()},
            {"slopes", slopesArr.size()},
            {"hazards", hazardsArr.size()},
            {"interactives", interactivesArr.size()},
            {"deco", decoCount},
        })},
        {"blocks", blocksArr},
        {"slopes", slopesArr},
        {"hazards", hazardsArr},
        {"interactives", interactivesArr},
    }));
}

matjson::Value toolAuditLevelQuality(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return errorResult("No active LevelEditorLayer.");

    bool const structureOnly = args["structure_only"].asBool().unwrapOr(false);
    float const margin = paimon::json::floatOr(args["margin"], 90.f);

    WorldBounds coreBounds;
    if (args.contains("bounds") && args["bounds"].isObject()) {
        coreBounds.expand(
            paimon::json::floatOr(args["bounds"]["x_min"], 0.f),
            paimon::json::floatOr(args["bounds"]["y_min"], 0.f)
        );
        coreBounds.expand(
            paimon::json::floatOr(args["bounds"]["x_max"], 0.f),
            paimon::json::floatOr(args["bounds"]["y_max"], 0.f)
        );
    } else {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            ObjectRole const role = classifyObjectRole(obj);
            if (role == ObjectRole::SolidBlock || role == ObjectRole::Slope ||
                role == ObjectRole::Hazard || role == ObjectRole::OrbOrPad) {
                coreBounds.expand(obj->getPositionX(), obj->getPositionY() - kGroundOffsetY, 15.f, 15.f);
            }
        }
    }

    auto issues = matjson::Value::array();
    auto pushIssue = [&](char const* severity, char const* kind, GameObject* obj, std::string msg) {
        if (issues.size() >= 80) return;
        auto item = matjson::makeObject({
            {"severity", severity},
            {"kind", kind},
            {"message", std::move(msg)},
        });
        if (obj) {
            item["uniqueID"] = obj->m_uniqueID;
            item["objectID"] = obj->m_objectID;
            item["x"] = obj->getPositionX();
            item["worldY"] = obj->getPositionY() - kGroundOffsetY;
        }
        issues.push(std::move(item));
    };

    int scatteredCount = 0;
    int missingNoTouchCount = 0;
    int belowGroundCount = 0;
    int bgLayerInStructureModeCount = 0;
    int duplicateCount = 0;
    int defaultBleedCount = 0;
    int skewedRotCount = 0;
    int brokenTriggerCount = 0;
    int colorIssueCount = 0;

    // Precompute group membership & default solid cells & color trigger targets
    std::map<int, int> groupMemberCounts;
    std::map<int, int> groupParentCounts;
    std::set<std::pair<int, int>> defaultSolidCells;
    std::set<int> usedColorChannels;
    std::set<int> colorTriggerTargets;
    std::set<std::string> seenTransforms;

    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        for (int g : objectGroups(obj)) {
            ++groupMemberCounts[g];
            if (obj->m_hasGroupParent) ++groupParentCounts[g];
        }
        if (obj->m_isTrigger) {
            if (obj->m_objectID == 899 || obj->m_objectID == 1006) {
                auto pairs = parseSavePairs(std::string(obj->getSaveString(lel)));
                auto tc = findSavePairValue(pairs, obj->m_objectID == 899 ? "23" : "51");
                int cid = 0;
                if (!tc.empty()) {
                    std::from_chars(tc.data(), tc.data() + tc.size(), cid);
                    if (cid > 0) colorTriggerTargets.insert(cid);
                }
            }
            continue;
        }
        int const id = obj->m_objectID;
        if (obj->m_baseColor && obj->m_baseColor->m_colorID > 0) usedColorChannels.insert(obj->m_baseColor->m_colorID);
        if (obj->m_detailColor && obj->m_detailColor->m_colorID > 0) usedColorChannels.insert(obj->m_detailColor->m_colorID);
        if (!obj->m_isHide && !obj->m_isNoTouch && ((id >= 1 && id <= 7) || id == 289 || id == 290 || id == 291 || id == 292)) {
            int const gx = static_cast<int>(std::llround((obj->getPositionX() - 15.f) / kBlockSizeUnits));
            int const gy = static_cast<int>(std::llround(((obj->getPositionY() - kGroundOffsetY) - 15.f) / kBlockSizeUnits));
            defaultSolidCells.insert({gx, gy});
        }
    }

    auto* em = getEffectManager(lel);
    bool bgIsBright = false;
    if (em) {
        if (auto* bg = em->getColorAction(1000)) {
            float const lum = (0.2126f * bg->m_fromColor.r + 0.7152f * bg->m_fromColor.g + 0.0722f * bg->m_fromColor.b) / 255.f;
            bgIsBright = lum > 0.72f;
        }
        for (int cid : usedColorChannels) {
            auto* ca = em->getColorAction(cid);
            if (!ca) continue;
            if (cid < 1000 && ca->m_fromOpacity <= 0.01f && ca->m_copyID == 0 && !colorTriggerTargets.contains(cid)) {
                ++colorIssueCount;
                pushIssue("warning", "invisible_color_channel", nullptr,
                    fmt::format("Color channel {} is referenced by objects but has opacity ~0 and no Color trigger targeting it.", cid));
            }
            if (bgIsBright && ca->m_blending && cid < 1000) {
                ++colorIssueCount;
                pushIssue("warning", "additive_washout", nullptr,
                    fmt::format("Color channel {} uses additive blending=true on a bright BG (channel 1000), which may wash colors out to white.", cid));
            }
        }
    }

    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        float const wx = obj->getPositionX();
        float const wy = obj->getPositionY() - kGroundOffsetY;
        int const id = obj->m_objectID;
        int const zInt = static_cast<int>(obj->m_zLayer);

        if (obj->m_isTrigger) {
            if (auto* eff = typeinfo_cast<EffectGameObject*>(obj)) {
                if (eff->m_isSpawnTriggered && objectGroups(obj).empty()) {
                    ++brokenTriggerCount;
                    pushIssue("error", "spawn_without_group", obj,
                        fmt::format("Spawn-triggered trigger {} at ({:.1f}, {:.1f}) has no assigned group ID and can never fire.", id, wx, wy));
                }
                bool const usesTargetGroup = (id == 901 || id == 1007 || id == 1049 || id == 1268 ||
                    id == 1346 || id == 1347 || id == 1585 || id == 1616 || id == 1814 || id == 2067 ||
                    (id >= 3006 && id <= 3015));
                if (usesTargetGroup && eff->m_targetGroupID > 0 && !groupMemberCounts.contains(eff->m_targetGroupID)) {
                    ++brokenTriggerCount;
                    pushIssue("warning", "empty_target_group", obj,
                        fmt::format("Trigger {} targets group {} which currently has 0 objects.", id, eff->m_targetGroupID));
                }
                if ((id == 1346 || id == 1347 || id == 2067) && eff->m_centerGroupID > 0) {
                    int const cnt = groupMemberCounts.contains(eff->m_centerGroupID) ? groupMemberCounts[eff->m_centerGroupID] : 0;
                    if (cnt == 0) {
                        ++brokenTriggerCount;
                        pushIssue("warning", "empty_center_group", obj,
                            fmt::format("Trigger {} uses center_group {} which has 0 objects.", id, eff->m_centerGroupID));
                    } else if (cnt > 1 && (!groupParentCounts.contains(eff->m_centerGroupID) || groupParentCounts[eff->m_centerGroupID] == 0)) {
                        ++brokenTriggerCount;
                        pushIssue("warning", "center_group_missing_parent", obj,
                            fmt::format("Trigger {} uses multi-object center_group {} without group_parent=true.", id, eff->m_centerGroupID));
                    }
                }
            }
            continue;
        }

        // Duplicate / stacked transform detection
        std::string const sig = fmt::format("{}|{:.1f}|{:.1f}|{:.1f}|{:.2f}|{:.2f}|{}|{}|{}|{}",
            id, wx, wy, obj->getRotation(), obj->m_scaleX, obj->m_scaleY,
            obj->isFlipX() ? 1 : 0, obj->isFlipY() ? 1 : 0, zInt, obj->m_zOrder);
        if (!seenTransforms.insert(sig).second) {
            ++duplicateCount;
            pushIssue("warning", "duplicate_overlap", obj,
                fmt::format("Duplicate object {} stacked identically at ({:.1f}, {:.1f}) on {}.", id, wx, wy, zLayerName(zInt)));
        }

        if (wy < -15.f && !obj->m_isHide) {
            ++belowGroundCount;
            pushIssue("warning", "below_ground", obj,
                fmt::format("Object {} at x={:.1f}, worldY={:.1f} is below ground Y=0.", id, wx, wy));
        }

        // Deco shape placed without no_touch=true can create accidental hitboxes
        bool const isCommonDecoPrimitive = (id == 211 || id == 693 || id == 694 || id == 3637 ||
                                            id == 3636 || id == 1764 || id == 503 ||
                                            (id >= 997 && id <= 1000) || id == 914);
        if (isCommonDecoPrimitive && !obj->m_isNoTouch && !obj->m_isHide &&
            (zInt != 0 || std::abs(obj->m_scaleX - 1.f) > 0.05f || std::abs(obj->m_scaleY - 1.f) > 0.05f)) {
            ++missingNoTouchCount;
            pushIssue("warning", "missing_no_touch", obj,
                fmt::format("Decorative/scaled object {} lacks no_touch=true and may interfere with hitboxes.", id));
        }

        if (isCommonDecoPrimitive && zInt <= 0 && !obj->m_isHide) {
            int const gx = static_cast<int>(std::llround((wx - 15.f) / kBlockSizeUnits));
            int const gy = static_cast<int>(std::llround((wy - 15.f) / kBlockSizeUnits));
            if (defaultSolidCells.contains({gx, gy})) {
                ++defaultBleedCount;
                pushIssue("warning", "default_cover_bleed", obj,
                    fmt::format("Cover primitive {} at ({:.1f}, {:.1f}) is on z_layer={} over a default block/slope; move to z_layer='T2' so default sprites do not bleed through.", id, wx, wy, zLayerName(zInt)));
            }
        }

        // Non-uniform scale + non-90deg rotation skews vector primitives in GD
        if (isCommonDecoPrimitive && std::abs(obj->m_scaleX - obj->m_scaleY) > 0.05f) {
            float const r90 = std::fmod(std::abs(obj->getRotation()), 90.f);
            if (r90 > 1.f && r90 < 89.f) {
                ++skewedRotCount;
                pushIssue("warning", "non_uniform_skew_rotation", obj,
                    fmt::format("Object {} at ({:.1f}, {:.1f}) combines scaleX!=scaleY ({:.2f}x{:.2f}) with non-90deg rotation ({:.1f}), causing visual skew.", id, wx, wy, obj->m_scaleX, obj->m_scaleY, obj->getRotation()));
            }
        }

        if (structureOnly && (zInt == -5 || zInt == -3)) {
            ++bgLayerInStructureModeCount;
            pushIssue("error", "unrequested_background_layer", obj,
                fmt::format("Object {} is on background Z-layer {} during structure_only audit.", id, zLayerName(zInt)));
        }

        if (coreBounds.valid) {
            bool const outsideCore = wx < (coreBounds.minX - margin) || wx > (coreBounds.maxX + margin) ||
                                     wy < (coreBounds.minY - margin) || wy > (coreBounds.maxY + margin);
            if (outsideCore) {
                ++scatteredCount;
                pushIssue("warning", "scattered_orphan", obj,
                    fmt::format("Object {} at ({:.1f}, {:.1f}) is >{:.0f}u outside the main structure bounds.", id, wx, wy, margin));
            }
        }
    }

    int errorCount = 0;
    for (auto const& it : issues) {
        if (it["severity"].asString().unwrapOr("") == "error") ++errorCount;
    }
    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"passed", issues.size() == 0},
        {"structureOnly", structureOnly},
        {"coreBoundingBox", coreBounds.toJson()},
        {"issueCount", issues.size()},
        {"errorCount", errorCount},
        {"summary", matjson::makeObject({
            {"scatteredOrphans", scatteredCount},
            {"missingNoTouch", missingNoTouchCount},
            {"belowGround", belowGroundCount},
            {"unrequestedBackgroundLayer", bgLayerInStructureModeCount},
            {"duplicateOverlaps", duplicateCount},
            {"defaultCoverBleeds", defaultBleedCount},
            {"skewedRotations", skewedRotCount},
            {"brokenTriggerWiring", brokenTriggerCount},
            {"colorIssues", colorIssueCount},
        })},
        {"issues", issues},
    }));
}

matjson::Value toolGetSpriteMetrics(matjson::Value const& args) {
    std::vector<int> ids;
    if (args.contains("object_id")) {
        int id = paimon::json::integerOr<int>(args["object_id"], 0);
        if (id > 0) ids.push_back(id);
    }
    if (args.contains("object_ids") && args["object_ids"].isArray()) {
        for (auto const& v : args["object_ids"]) {
            int id = paimon::json::integerOr<int>(v, 0);
            if (id > 0 && static_cast<int>(ids.size()) < 40) ids.push_back(id);
        }
    }
    if (ids.empty()) return errorResult("Provide object_id or non-empty object_ids array.");

    auto* tb = ObjectToolbox::sharedState();
    auto* sfc = CCSpriteFrameCache::sharedSpriteFrameCache();
    auto items = matjson::Value::array();

    for (int id : ids) {
        std::string frame;
        if (tb) {
            if (char const* f = tb->intKeyToFrame(id)) frame = f;
        }
        float w = 30.f, h = 30.f, offX = 0.f, offY = 0.f;
        bool frameLoaded = false;
        if (sfc && !frame.empty()) {
            if (auto* sf = sfc->spriteFrameByName(frame.c_str())) {
                CCSize const orig = sf->getOriginalSizeInPixels();
                CCSize const rectSz = sf->getRectInPixels().size;
                CCPoint const off = sf->getOffset();
                float const scaleFactor = CCDirector::get()->getContentScaleFactor();
                float const inv = scaleFactor > 0.f ? (1.f / scaleFactor) : 1.f;
                w = (orig.width > 0.f ? orig.width : rectSz.width) * inv;
                h = (orig.height > 0.f ? orig.height : rectSz.height) * inv;
                offX = off.x;
                offY = off.y;
                frameLoaded = true;
            }
        }
        std::string note;
        for (auto const& c : curatedObjectCatalog()) {
            if (c.id == id) { note = c.notes; break; }
        }
        auto entry = matjson::makeObject({
            {"objectID", id},
            {"frame", frame},
            {"frameLoaded", frameLoaded},
            {"widthUnits", w},
            {"heightUnits", h},
            {"offsetX", offX},
            {"offsetY", offY},
        });
        if (!note.empty()) entry["notes"] = note;
        items.push(std::move(entry));
    }

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"count", items.size()},
        {"metrics", items},
    }));
}

matjson::Value toolValidateGameplayPath(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return errorResult("No active LevelEditorLayer.");

    float const xMin = paimon::json::floatOr(args["x_min"], -1e9f);
    float const xMax = paimon::json::floatOr(args["x_max"], 1e9f);

    int startMode = 0;
    int startSpeed = 0;
    bool startMini = false;
    if (lel->m_levelSettings) {
        startMode = static_cast<int>(lel->m_levelSettings->m_startMode);
        startSpeed = static_cast<int>(lel->m_levelSettings->m_startSpeed);
        startMini = lel->m_levelSettings->m_startMini;
    }

    auto speedToUnitsPerSec = [](int spd) -> float {
        switch (spd) {
            case 1: return 251.16f;
            case 2: return 387.42f;
            case 3: return 468.0f;
            case 4: return 576.0f;
            default: return 311.58f;
        }
    };

    struct Plat { float x; float y; float hw; float hh; int uid; int id; };
    std::vector<Plat> solids;
    std::vector<Plat> hazards;
    std::vector<Plat> portals;
    std::vector<Plat> interactives;
    std::vector<std::pair<float, int>> speedPortals;
    std::vector<std::pair<float, int>> modePortals;
    bool hasEndTrigger = false;
    float maxX = 0.f;

    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        float const wx = obj->getPositionX();
        if (wx < xMin || wx > xMax) continue;
        float const wy = obj->getPositionY() - kGroundOffsetY;
        maxX = std::max(maxX, wx);
        if (obj->m_objectID == 3600) hasEndTrigger = true;
        if (obj->m_isTrigger) continue;

        ObjectRole const role = classifyObjectRole(obj);
        float const hw = 15.f * std::abs(obj->m_scaleX);
        float const hh = 15.f * std::abs(obj->m_scaleY);
        if (role == ObjectRole::SolidBlock || role == ObjectRole::Slope) {
            solids.push_back({wx, wy, hw, hh, obj->m_uniqueID, obj->m_objectID});
        } else if (role == ObjectRole::Hazard) {
            hazards.push_back({wx, wy, hw * 0.65f, hh * 0.7f, obj->m_uniqueID, obj->m_objectID});
        } else if (role == ObjectRole::OrbOrPad) {
            interactives.push_back({wx, wy, hw, hh, obj->m_uniqueID, obj->m_objectID});
        } else if (role == ObjectRole::Portal) {
            portals.push_back({wx, wy, hw, hh, obj->m_uniqueID, obj->m_objectID});
            if (obj->m_objectID == 200) speedPortals.emplace_back(wx, 1);
            else if (obj->m_objectID == 201) speedPortals.emplace_back(wx, 0);
            else if (obj->m_objectID == 202) speedPortals.emplace_back(wx, 2);
            else if (obj->m_objectID == 203) speedPortals.emplace_back(wx, 3);
            else if (obj->m_objectID == 1334) speedPortals.emplace_back(wx, 4);
            else if (obj->m_objectID == 12) modePortals.emplace_back(wx, 0);
            else if (obj->m_objectID == 13) modePortals.emplace_back(wx, 1);
            else if (obj->m_objectID == 47) modePortals.emplace_back(wx, 2);
            else if (obj->m_objectID == 111) modePortals.emplace_back(wx, 3);
            else if (obj->m_objectID == 660) modePortals.emplace_back(wx, 4);
            else if (obj->m_objectID == 745) modePortals.emplace_back(wx, 5);
            else if (obj->m_objectID == 1331) modePortals.emplace_back(wx, 6);
            else if (obj->m_objectID == 1933) modePortals.emplace_back(wx, 7);
        }
    }

    auto warnings = matjson::Value::array();
    // Check if any hazard overlaps a solid block core (unfair/broken placement)
    for (auto const& h : hazards) {
        for (auto const& s : solids) {
            bool const overlapX = std::abs(h.x - s.x) < (h.hw * 0.5f + s.hw * 0.85f);
            bool const overlapY = std::abs(h.y - s.y) < (h.hh * 0.5f + s.hh * 0.85f);
            if (overlapX && overlapY) {
                warnings.push(matjson::makeObject({
                    {"kind", "hazard_inside_solid"},
                    {"uniqueID", h.uid},
                    {"x", h.x}, {"worldY", h.y},
                    {"message", fmt::format("Hazard {} overlaps solid block {} at ({:.1f}, {:.1f}).", h.uid, s.uid, h.x, h.y)},
                }));
                break;
            }
        }
    }

    // Check if any portal overlaps a solid block
    for (auto const& p : portals) {
        for (auto const& s : solids) {
            bool const overlapX = std::abs(p.x - s.x) < (p.hw * 0.45f + s.hw * 0.85f);
            bool const overlapY = std::abs(p.y - s.y) < (p.hh * 0.6f + s.hh * 0.85f);
            if (overlapX && overlapY) {
                warnings.push(matjson::makeObject({
                    {"kind", "portal_inside_solid"},
                    {"uniqueID", p.uid},
                    {"x", p.x}, {"worldY", p.y},
                    {"message", fmt::format("Portal {} overlaps solid block {} at ({:.1f}, {:.1f}).", p.uid, s.uid, p.x, p.y)},
                }));
                break;
            }
        }
    }

    // Integrate duration segment-by-segment across speed portals
    std::sort(speedPortals.begin(), speedPortals.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::sort(modePortals.begin(), modePortals.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

    float estDurationSec = 0.f;
    float prevX = 0.f;
    int curSpeed = startSpeed;
    for (auto const& [px, spd] : speedPortals) {
        if (px > prevX) {
            estDurationSec += (px - prevX) / speedToUnitsPerSec(curSpeed);
            prevX = px;
        }
        curSpeed = spd;
    }
    if (maxX > prevX) {
        estDurationSec += (maxX - prevX) / speedToUnitsPerSec(curSpeed);
    }

    // Detect overly wide contiguous spike clusters at the same Y in Cube segments
    std::sort(hazards.begin(), hazards.end(), [](auto const& a, auto const& b) { return a.x < b.x; });
    auto activeSpeedAt = [&](float x) {
        int s = startSpeed;
        for (auto const& [px, spd] : speedPortals) {
            if (px <= x) s = spd;
            else break;
        }
        return speedToUnitsPerSec(s);
    };
    auto activeModeAt = [&](float x) {
        int m = startMode;
        for (auto const& [px, md] : modePortals) {
            if (px <= x) m = md;
            else break;
        }
        return m;
    };

    for (size_t i = 0; i < hazards.size();) {
        if (activeModeAt(hazards[i].x) != 0) { ++i; continue; } // check Cube mode clusters
        float startX = hazards[i].x - hazards[i].hw;
        float endX = hazards[i].x + hazards[i].hw;
        float baseY = hazards[i].y;
        int clusterCount = 1;
        size_t j = i + 1;
        while (j < hazards.size() && std::abs(hazards[j].y - baseY) < 12.f &&
               (hazards[j].x - hazards[j].hw) - endX <= 12.f) {
            endX = std::max(endX, hazards[j].x + hazards[j].hw);
            ++clusterCount;
            ++j;
        }
        float const span = endX - startX;
        float const jumpSpan = activeSpeedAt(hazards[i].x) * (startMini ? 0.34f : 0.433f);
        if (clusterCount >= 3 && span > jumpSpan * 1.15f) {
            bool hasHelper = false;
            for (auto const& it : interactives) {
                if (it.x >= startX - 45.f && it.x <= endX + 15.f) { hasHelper = true; break; }
            }
            if (!hasHelper) {
                warnings.push(matjson::makeObject({
                    {"kind", "impossible_cube_spike_cluster"},
                    {"uniqueID", hazards[i].uid},
                    {"x", startX}, {"worldY", baseY},
                    {"message", fmt::format("Cube hazard cluster ({} hazards, span {:.1f}u) near x={:.1f} exceeds single jump span (~{:.1f}u) with no orb/pad.", clusterCount, span, startX, jumpSpan)},
                }));
            }
        }
        i = j;
    }

    float const baseSpeed = speedToUnitsPerSec(startSpeed);
    float const estCubeJumpSpan = baseSpeed * (startMini ? 0.34f : 0.433f);

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"startMode", startMode},
        {"startSpeed", startSpeed},
        {"startMini", startMini},
        {"baseSpeedUnitsPerSec", baseSpeed},
        {"estimatedCubeJumpHeight", startMini ? 48.0f : 65.0f},
        {"estimatedCubeJumpSpan", estCubeJumpSpan},
        {"maxX", maxX},
        {"estimatedDurationSeconds", estDurationSec},
        {"hasEndTrigger", hasEndTrigger},
        {"counts", matjson::makeObject({
            {"solids", solids.size()},
            {"hazards", hazards.size()},
            {"portals", portals.size()},
            {"speedPortals", speedPortals.size()},
            {"modePortals", modePortals.size()},
        })},
        {"warningCount", warnings.size()},
        {"warnings", warnings},
    }));
}

matjson::Value toolMirrorOrCloneStructure(matjson::Value const& args) {
    auto* lel = LevelEditorLayer::get();
    if (!lel) return errorResult("No active LevelEditorLayer.");

    auto targets = findObjectsWorldFilter(lel, args);
    if (targets.empty()) return errorResult("No matching objects found to clone/mirror.");

    bool const mirrorX = args["mirror_x"].asBool().unwrapOr(false);
    bool const mirrorY = args["mirror_y"].asBool().unwrapOr(false);
    float const dx = paimon::json::floatOr(args["dx"], 0.f);
    float const dy = paimon::json::floatOr(args["dy"], 0.f);

    float sumX = 0.f, sumY = 0.f;
    for (auto const& [obj, _] : targets) {
        if (!obj) continue;
        sumX += obj->getPositionX();
        sumY += (obj->getPositionY() - kGroundOffsetY);
    }
    float const pivotX = args.contains("pivot_x")
        ? paimon::json::floatOr(args["pivot_x"], sumX / targets.size())
        : (sumX / targets.size());
    float const pivotY = args.contains("pivot_y")
        ? paimon::json::floatOr(args["pivot_y"], sumY / targets.size())
        : (sumY / targets.size());

    int assignGroup = paimon::json::integerOr<int>(args["new_group_id"], 0);
    if (assignGroup <= 0 && args["allocate_group"].asBool().unwrapOr(false)) {
        std::unordered_set<int> emptyExclude;
        assignGroup = lel->getNextFreeGroupID(emptyExclude);
    }

    std::map<int, int> colorRemap;
    if (args.contains("color_map") && args["color_map"].isObject()) {
        for (auto const& [k, v] : args["color_map"]) {
            int src = 0;
            auto const* b = k.data();
            auto const* e = b + k.size();
            auto res = std::from_chars(b, e, src);
            int dst = paimon::json::integerOr<int>(v, 0);
            if (res.ec == std::errc{} && res.ptr == e && src > 0 && dst > 0) colorRemap[src] = dst;
        }
    }

    std::string combined;
    for (auto const& [obj, _] : targets) {
        if (!obj) continue;
        auto pairs = parseSavePairs(std::string(obj->getSaveString(lel)));

        float wx = obj->getPositionX();
        float wy = obj->getPositionY() - kGroundOffsetY;
        if (mirrorX) wx = pivotX - (wx - pivotX);
        if (mirrorY) wy = pivotY - (wy - pivotY);
        wx += dx;
        wy += dy;
        setSavePair(pairs, "2", formatFloat(wx));
        setSavePair(pairs, "3", formatFloat(wy));

        if (mirrorX != mirrorY) {
            float const rot = -obj->getRotation();
            setSavePair(pairs, "6", formatFloat(rot));
        }
        if (mirrorX) setSavePair(pairs, "4", obj->isFlipX() ? "0" : "1");
        if (mirrorY) setSavePair(pairs, "5", obj->isFlipY() ? "0" : "1");

        if (!colorRemap.empty()) {
            int base = obj->m_baseColor ? obj->m_baseColor->m_colorID : 0;
            int det = obj->m_detailColor ? obj->m_detailColor->m_colorID : 0;
            if (auto it = colorRemap.find(base); it != colorRemap.end()) setSavePair(pairs, "21", std::to_string(it->second));
            if (auto it = colorRemap.find(det); it != colorRemap.end()) setSavePair(pairs, "22", std::to_string(it->second));
        }
        if (assignGroup > 0) {
            setSavePair(pairs, "57", std::to_string(assignGroup));
        }

        combined += joinSavePairs(pairs);
    }

    bool const noUndo = args["no_undo"].asBool().unwrapOr(false);
    CCArray* created = lel->createObjectsFromString(combined, noUndo, true);
    if (!created || created->count() == 0) {
        return errorResult("Failed to clone/mirror structure.");
    }
    finishEditorMutation(lel, created);
    EditorMcpServer::get().recordObjectsCreated(created->count());

    return jsonResult(matjson::makeObject({
        {"ok", true},
        {"clonedCount", static_cast<int>(created->count())},
        {"assignedGroupID", assignGroup},
        {"mirrorX", mirrorX},
        {"mirrorY", mirrorY},
    }));
}

} // namespace

matjson::Value invokeMcpTool(std::string_view name, matjson::Value const& args) {
    EditorMcpServer::get().recordToolCall(name);

    // Pure catalog reference doesn't require main thread
    if (name == "get_gd_reference") {
        std::string const topic = paimon::json::stringOr(args["topic"], "all");
        std::string const filter = paimon::json::stringOr(args["filter"], "");
        return jsonResult(buildReferenceJson(topic, filter));
    }

    if (name == "search_object_catalog") {
        std::string const q = paimon::json::stringOr(args["query"], "");
        std::string const cat = paimon::json::stringOr(args["category"], "");
        int const limit = paimon::json::integerOr<int>(args["limit"], 40);
        return runOnMainThread([q, cat, limit]() {
            return jsonResult(searchObjectCatalog(q, cat, limit, true));
        });
    }

    std::string const toolName(name);
    matjson::Value const argsCopy = args;
    if (toolName == "open_level_in_editor") {
        std::shared_ptr<SceneSwapWaiter> waiter;
        auto res = runOnMainThread([argsCopy, &waiter]() {
            return toolOpenLevelInEditor(argsCopy, waiter);
        });
        if (waiter) {
            waiter->waitFor(std::chrono::milliseconds(2500));
        }
        return res;
    }

    return runOnMainThread([toolName, argsCopy]() -> matjson::Value {
        if (toolName == "get_editor_state") return toolGetEditorState();
        if (toolName == "list_local_levels") return toolListLocalLevels();
        if (toolName == "save_level") return toolSaveLevel();
        if (toolName == "get_objects") return toolGetObjects(argsCopy);
        if (toolName == "get_object_raw") return toolGetObjectRaw(argsCopy);
        if (toolName == "place_object") return toolPlaceObject(argsCopy);
        if (toolName == "place_objects_batch") return toolPlaceObjectsBatch(argsCopy);
        if (toolName == "place_trigger") return toolPlaceTrigger(argsCopy);
        if (toolName == "update_objects") return toolUpdateObjects(argsCopy);
        if (toolName == "delete_objects") return toolDeleteObjects(argsCopy);
        if (toolName == "transform_objects") return toolTransformObjects(argsCopy);
        if (toolName == "select_objects") return toolSelectObjects(argsCopy);
        if (toolName == "undo") return toolUndoRedo(false);
        if (toolName == "redo") return toolUndoRedo(true);
        if (toolName == "manage_groups") return toolManageGroups(argsCopy);
        if (toolName == "get_color_channels") return toolGetColorChannels(argsCopy);
        if (toolName == "set_color_channel") return toolSetColorChannel(argsCopy);
        if (toolName == "get_level_settings") return toolGetLevelSettings();
        if (toolName == "set_level_settings") return toolSetLevelSettings(argsCopy);
        if (toolName == "set_editor_camera") return toolSetEditorCameraAndOptions(argsCopy);
        if (toolName == "render_1to1") return toolRender1To1(argsCopy);
        if (toolName == "inspect_structure_bounds") return toolInspectStructureBounds(argsCopy);
        if (toolName == "audit_level_quality") return toolAuditLevelQuality(argsCopy);
        if (toolName == "get_sprite_metrics") return toolGetSpriteMetrics(argsCopy);
        if (toolName == "validate_gameplay_path") return toolValidateGameplayPath(argsCopy);
        if (toolName == "mirror_or_clone_structure") return toolMirrorOrCloneStructure(argsCopy);
        return errorResult(fmt::format("Unknown MCP tool: {}", toolName));
    });
}

RenderResult capturePreviewMainThread(RenderRequest const& req) {
    return runOnMainThread([req]() {
        auto res = renderEditor1To1(req);
        if (res.ok) EditorMcpServer::get().recordRenderServed();
        return res;
    });
}

} // namespace paimon::editormcp
