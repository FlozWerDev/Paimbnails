#include "CustomHover.hpp"
#include "../../utils/MainThreadDelay.hpp"

#include <utility>

using namespace geode::prelude;

namespace paimon::hover {
namespace {

matjson::Value encode(Config const& config) {
    auto json = matjson::Value::object();
    json["enabled"] = config.enabled;
    json["scale"] = config.scale;
    json["stretch"] = config.stretch;
    json["lift"] = config.lift;
    json["slide"] = config.slide;
    json["rotation"] = config.rotation;
    json["enter"] = config.enter;
    json["exit"] = config.exit;
    json["delay"] = config.delay;
    json["amplitude"] = config.amplitude;
    json["frequency"] = config.frequency;
    json["easing"] = config.easing;
    json["loop"] = config.loop;
    return json;
}

Config decode(matjson::Value const& json) {
    Config config;
    config.enabled = json["enabled"].asBool().unwrapOr(config.enabled);
    config.scale = static_cast<float>(json["scale"].asDouble().unwrapOr(config.scale));
    config.stretch = static_cast<float>(json["stretch"].asDouble().unwrapOr(config.stretch));
    config.lift = static_cast<float>(json["lift"].asDouble().unwrapOr(config.lift));
    config.slide = static_cast<float>(json["slide"].asDouble().unwrapOr(config.slide));
    config.rotation = static_cast<float>(json["rotation"].asDouble().unwrapOr(config.rotation));
    config.enter = static_cast<float>(json["enter"].asDouble().unwrapOr(config.enter));
    config.exit = static_cast<float>(json["exit"].asDouble().unwrapOr(config.exit));
    config.delay = static_cast<float>(json["delay"].asDouble().unwrapOr(config.delay));
    config.amplitude = static_cast<float>(json["amplitude"].asDouble().unwrapOr(config.amplitude));
    config.frequency = static_cast<float>(json["frequency"].asDouble().unwrapOr(config.frequency));
    config.easing = static_cast<int>(json["easing"].asInt().unwrapOr(config.easing));
    config.loop = static_cast<int>(json["loop"].asInt().unwrapOr(config.loop));
    return sanitize(config);
}

} // namespace

Manager& Manager::get() {
    static Manager manager;
    return manager;
}

void Manager::load() {
    auto* mod = Mod::get();
    if (!mod) return;

    auto json = mod->getSavedValue<matjson::Value>(
        "custom-hover-v1", matjson::Value::object());
    global = decode(json["global"]);
    linked = json["linked"].asBool().unwrapOr(false);
    buttons.clear();
    saved.clear();
    picking = false;
    m_dirty = false;

    if (auto entries = json["buttons"].asArray()) {
        for (auto const& entry : entries.unwrap()) {
            auto key = entry["key"].asString().unwrapOr("");
            if (!key.empty() && buttons.size() < 2048) {
                buttons[key] = decode(entry["config"]);
            }
        }
    }
    if (auto entries = json["presets"].asArray()) {
        for (auto const& entry : entries.unwrap()) {
            auto name = entry["key"].asString().unwrapOr("");
            if (!name.empty() && saved.size() < 100) {
                saved[name.substr(0, 24)] = decode(entry["config"]);
            }
        }
    }
    ++m_revision;
}

void Manager::save() {
    auto* mod = Mod::get();
    if (!mod) return;

    auto json = matjson::Value::object();
    json["global"] = encode(global);
    json["linked"] = linked;

    auto pack = [](auto const& entries) {
        std::vector<matjson::Value> result;
        result.reserve(entries.size());
        for (auto const& [key, config] : entries) {
            auto entry = matjson::Value::object();
            entry["key"] = key;
            entry["config"] = encode(config);
            result.push_back(std::move(entry));
        }
        return matjson::Value(result);
    };

    json["buttons"] = pack(buttons);
    json["presets"] = pack(saved);
    mod->setSavedValue("custom-hover-v1", json);
    m_dirty = false;
    paimon::requestDeferredModSave();
}

void Manager::saveIfDirty() {
    if (m_dirty) {
        save();
    }
}

Config Manager::resolve(std::string const& key) const {
    if (!linked && !key.empty()) {
        if (auto it = buttons.find(key); it != buttons.end()) {
            return it->second;
        }
    }
    return global;
}

bool Manager::hasOverride(std::string const& key) const {
    return !key.empty() && buttons.find(key) != buttons.end();
}

void Manager::set(std::string const& key, Config config, bool persist) {
    config = sanitize(config);
    if (key.empty() || linked) {
        global = config;
    } else if (buttons.size() < 2048 || buttons.find(key) != buttons.end()) {
        buttons[key] = config;
    }
    ++m_revision;
    if (persist) {
        save();
    } else {
        m_dirty = true;
    }
}

void Manager::removeButton(std::string const& key) {
    if (key.empty()) return;
    if (buttons.erase(key) > 0) {
        ++m_revision;
        save();
    }
}

void Manager::setLinked(bool value) {
    if (linked == value) return;
    linked = value;
    ++m_revision;
    save();
}

void Manager::group(Config config) {
    global = sanitize(config);
    linked = true;
    ++m_revision;
    save();
}

bool Manager::savePreset(std::string const& name, Config config) {
    auto trimmed = name.substr(0, 24);
    if (trimmed.empty()) return false;
    if (saved.size() >= 100 && saved.find(trimmed) == saved.end()) {
        return false;
    }
    saved[trimmed] = sanitize(config);
    ++m_revision;
    save();
    return true;
}

void Manager::deletePreset(std::string const& name) {
    if (saved.erase(name) > 0) {
        ++m_revision;
        save();
    }
}

void Manager::resetAll() {
    global = Config{};
    linked = false;
    picking = false;
    buttons.clear();
    saved.clear();
    ++m_revision;
    save();
}

void reset() {
    Manager::get().resetAll();
}

std::string buttonKey(CCNode* node) {
    if (!node) return {};
    std::string key;
    int depth = 0;
    for (auto* current = node;
         current && depth < 64 && !typeinfo_cast<CCScene*>(current);
         current = current->getParent(), ++depth) {
        // Must copy into an owning std::string: getID() returns geode::ZStringView.
        std::string id = std::string(current->getID());
        if (id.empty()) {
            unsigned index = 0;
            if (auto* parent = current->getParent()) {
                if (auto* children = parent->getChildren()) {
                    auto const found = children->indexOfObject(current);
                    if (found != UINT_MAX && found != CC_INVALID_INDEX) {
                        index = found;
                    }
                }
            }
            // type names differ across compilers, so anonymous nodes use their sibling index.
            id = fmt::format("node[{}]", index);
        }
        key = fmt::format("{}/{}{}", id.size(), id, key);
    }
    return key;
}

} // namespace paimon::hover
