#include "AnimateStore.hpp"

#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/file.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>

using namespace geode::prelude;

namespace paimon::animate {

namespace {

constexpr int kVersion = 1;
constexpr char const* kPrefsKey = "editor-animate-view-v1";

std::filesystem::path pathFor(std::string const& key) {
    return Mod::get()->getSaveDir() / "animate" / (key + ".json");
}

template <class E>
E enumOr(matjson::Value const& json, char const* key, E fallback, int count) {
    auto const raw = json[key].asInt().unwrapOr(static_cast<int>(fallback));
    if (raw < 0 || raw >= count) return fallback;
    return static_cast<E>(raw);
}

int intOr(matjson::Value const& json, char const* key, int fallback) {
    return static_cast<int>(json[key].asInt().unwrapOr(fallback));
}

float floatOr(matjson::Value const& json, char const* key, float fallback) {
    return static_cast<float>(json[key].asDouble().unwrapOr(fallback));
}

bool boolOr(matjson::Value const& json, char const* key, bool fallback) {
    return json[key].asBool().unwrapOr(fallback);
}

matjson::Value encodeClip(Clip const& clip) {
    auto json = matjson::Value::object();
    json["id"] = clip.id;
    json["name"] = clip.name;
    json["color"] = clip.color;
    json["hidden"] = clip.hidden;
    json["locked"] = clip.locked;

    auto const& s = clip.settings;
    auto settings = matjson::Value::object();
    settings["fps"] = s.fps;
    settings["mode"] = static_cast<int>(s.mode);
    settings["repeats"] = s.repeats;
    settings["end"] = static_cast<int>(s.end);
    settings["start"] = static_cast<int>(s.start);
    settings["before"] = static_cast<int>(s.before);
    settings["visibility"] = static_cast<int>(s.visibility);
    settings["startDelay"] = s.startDelay;
    settings["fade"] = s.fade;
    settings["startX"] = s.startX;
    settings["controls"] = s.controls;
    settings["stopAction"] = static_cast<int>(s.stopAction);
    settings["editorLayer"] = s.editorLayer;
    settings["compileOnSave"] = s.compileOnSave;
    json["settings"] = settings;

    std::vector<matjson::Value> frames;
    frames.reserve(clip.frames.size());
    for (auto const& frame : clip.frames) {
        auto entry = matjson::Value::object();
        entry["group"] = frame.group;
        entry["hold"] = frame.hold;
        if (!frame.label.empty()) entry["label"] = frame.label;
        if (frame.skip) entry["skip"] = true;
        frames.push_back(std::move(entry));
    }
    json["frames"] = matjson::Value(frames);

    auto const& c = clip.compiled;
    auto compiled = matjson::Value::object();
    compiled["marker"] = c.marker;
    compiled["chain"] = c.chain;
    compiled["start"] = c.start;
    compiled["stop"] = c.stop;
    compiled["pause"] = c.pause;
    compiled["resume"] = c.resume;
    compiled["triggers"] = c.triggers;
    compiled["steps"] = c.steps;
    compiled["stale"] = c.stale;
    json["compiled"] = compiled;
    return json;
}

Clip decodeClip(matjson::Value const& json) {
    Clip clip;
    clip.id = intOr(json, "id", 0);
    clip.name = json["name"].asString().unwrapOr("");
    clip.color = intOr(json, "color", 0);
    clip.hidden = boolOr(json, "hidden", false);
    clip.locked = boolOr(json, "locked", false);

    auto const& settings = json["settings"];
    auto& s = clip.settings;
    s.fps = floatOr(settings, "fps", s.fps);
    s.mode = enumOr(settings, "mode", s.mode, 5);
    s.repeats = intOr(settings, "repeats", s.repeats);
    s.end = enumOr(settings, "end", s.end, 3);
    s.start = enumOr(settings, "start", s.start, 3);
    s.before = enumOr(settings, "before", s.before, 3);
    s.visibility = enumOr(settings, "visibility", s.visibility, 2);
    s.startDelay = floatOr(settings, "startDelay", s.startDelay);
    s.fade = floatOr(settings, "fade", s.fade);
    s.startX = floatOr(settings, "startX", s.startX);
    s.controls = boolOr(settings, "controls", s.controls);
    s.stopAction = enumOr(settings, "stopAction", s.stopAction, 3);
    s.editorLayer = intOr(settings, "editorLayer", s.editorLayer);
    s.compileOnSave = boolOr(settings, "compileOnSave", s.compileOnSave);

    if (auto frames = json["frames"].asArray()) {
        for (auto const& entry : frames.unwrap()) {
            Frame frame;
            frame.group = intOr(entry, "group", 0);
            frame.hold = intOr(entry, "hold", 1);
            frame.label = entry["label"].asString().unwrapOr("");
            frame.skip = boolOr(entry, "skip", false);
            clip.frames.push_back(std::move(frame));
        }
    }

    auto const& compiled = json["compiled"];
    auto& c = clip.compiled;
    c.marker = intOr(compiled, "marker", 0);
    c.chain = intOr(compiled, "chain", 0);
    c.start = intOr(compiled, "start", 0);
    c.stop = intOr(compiled, "stop", 0);
    c.pause = intOr(compiled, "pause", 0);
    c.resume = intOr(compiled, "resume", 0);
    c.triggers = intOr(compiled, "triggers", 0);
    c.steps = intOr(compiled, "steps", 0);
    c.stale = boolOr(compiled, "stale", false);

    sanitize(clip);
    return clip;
}

} // namespace

std::string levelKey(GJGameLevel* level) {
    std::string name = level ? std::string(level->m_levelName) : std::string();
    if (name.empty()) name = "unnamed";

    // fnv-1a of the raw name keeps two names that sanitize alike apart.
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : name) {
        hash ^= c;
        hash *= 16777619u;
    }
    std::string safe;
    for (unsigned char c : name) {
        if (std::isalnum(c)) safe += static_cast<char>(std::tolower(c));
        else if (!safe.empty() && safe.back() != '-') safe += '-';
        if (safe.size() >= 40) break;
    }
    while (!safe.empty() && safe.back() == '-') safe.pop_back();
    if (safe.empty()) safe = "level";
    return fmt::format("{}-{:08x}", safe, hash);
}

matjson::Value encodeDocument(Document const& doc) {
    auto json = matjson::Value::object();
    json["version"] = kVersion;
    json["nextId"] = doc.nextId;
    json["activeClip"] = doc.activeClip;
    json["currentFrame"] = doc.currentFrame;
    std::vector<matjson::Value> clips;
    clips.reserve(doc.clips.size());
    for (auto const& clip : doc.clips) clips.push_back(encodeClip(clip));
    json["clips"] = matjson::Value(clips);
    return json;
}

Document decodeDocument(matjson::Value const& json) {
    Document doc;
    doc.nextId = std::max(1, intOr(json, "nextId", 1));
    doc.activeClip = intOr(json, "activeClip", 0);
    doc.currentFrame = std::max(0, intOr(json, "currentFrame", 0));
    if (auto clips = json["clips"].asArray()) {
        for (auto const& entry : clips.unwrap()) {
            if (doc.clips.size() >= static_cast<std::size_t>(kMaxClips)) break;
            auto clip = decodeClip(entry);
            if (clip.id <= 0) clip.id = doc.nextId;
            doc.nextId = std::max(doc.nextId, clip.id + 1);
            doc.clips.push_back(std::move(clip));
        }
    }
    return doc;
}

Document loadDocument(std::string const& key) {
    auto const path = pathFor(key);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    auto raw = geode::utils::file::readString(path);
    if (!raw) {
        log::warn("[Animate] No se pudo leer {}: {}", key, raw.unwrapErr());
        return {};
    }
    auto parsed = matjson::parse(raw.unwrap());
    if (!parsed) {
        log::warn("[Animate] JSON invalido en {}: {}", key, parsed.unwrapErr());
        return {};
    }
    return decodeDocument(parsed.unwrap());
}

bool saveDocument(std::string const& key, Document const& doc) {
    auto const path = pathFor(key);
    std::error_code ec;
    if (doc.clips.empty()) {
        std::filesystem::remove(path, ec);
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    bool const ok = paimon::file::writeAtomically(path, encodeDocument(doc).dump(matjson::NO_INDENTATION));
    if (!ok) log::error("[Animate] No se pudo guardar {}", key);
    return ok;
}

ViewPrefs loadPrefs() {
    auto const json = Mod::get()->getSavedValue<matjson::Value>(kPrefsKey, matjson::Value::object());
    ViewPrefs p;
    p.onion = boolOr(json, "onion", p.onion);
    p.onionBefore = intOr(json, "onionBefore", p.onionBefore);
    p.onionAfter = intOr(json, "onionAfter", p.onionAfter);
    p.onionOpacity = intOr(json, "onionOpacity", p.onionOpacity);
    p.ghosts = enumOr(json, "ghosts", p.ghosts, 3);
    p.dimOpacity = intOr(json, "dimOpacity", p.dimOpacity);
    p.lockOthers = boolOr(json, "lockOthers", p.lockOthers);
    p.autoAssign = boolOr(json, "autoAssign", p.autoAssign);
    p.previewLoop = boolOr(json, "previewLoop", p.previewLoop);
    p.followPlayhead = boolOr(json, "followPlayhead", p.followPlayhead);
    sanitize(p);
    return p;
}

void savePrefs(ViewPrefs const& prefs) {
    auto json = matjson::Value::object();
    json["onion"] = prefs.onion;
    json["onionBefore"] = prefs.onionBefore;
    json["onionAfter"] = prefs.onionAfter;
    json["onionOpacity"] = prefs.onionOpacity;
    json["ghosts"] = static_cast<int>(prefs.ghosts);
    json["dimOpacity"] = prefs.dimOpacity;
    json["lockOthers"] = prefs.lockOthers;
    json["autoAssign"] = prefs.autoAssign;
    json["previewLoop"] = prefs.previewLoop;
    json["followPlayhead"] = prefs.followPlayhead;
    Mod::get()->setSavedValue(kPrefsKey, json);
    paimon::requestDeferredModSave();
}

} // namespace paimon::animate
