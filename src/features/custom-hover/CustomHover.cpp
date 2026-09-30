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
    config.scale = json["scale"].asDouble().unwrapOr(config.scale);
    config.stretch = json["stretch"].asDouble().unwrapOr(config.stretch);
    config.lift = json["lift"].asDouble().unwrapOr(config.lift);
    config.slide = json["slide"].asDouble().unwrapOr(config.slide);
    config.rotation = json["rotation"].asDouble().unwrapOr(config.rotation);
    config.enter = json["enter"].asDouble().unwrapOr(config.enter);
    config.exit = json["exit"].asDouble().unwrapOr(config.exit);
    config.delay = json["delay"].asDouble().unwrapOr(config.delay);
    config.amplitude = json["amplitude"].asDouble().unwrapOr(config.amplitude);
    config.frequency = json["frequency"].asDouble().unwrapOr(config.frequency);
    config.easing = json["easing"].asInt().unwrapOr(config.easing);
    config.loop = json["loop"].asInt().unwrapOr(config.loop);
    return sanitize(config);
}

} // namespace

Manager& Manager::get() {
    static Manager manager;
    return manager;
}

void Manager::load() {
    auto json = Mod::get()->getSavedValue<matjson::Value>(
        "custom-hover-v1", matjson::Value::object());
    global = decode(json["global"]);
    linked = json["linked"].asBool().unwrapOr(false);
    buttons.clear();
    saved.clear();
    picking = false;

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
}

void Manager::save() {
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
    Mod::get()->setSavedValue("custom-hover-v1", json);
    paimon::requestDeferredModSave();
}

Config Manager::resolve(std::string const& key) const {
    auto it = buttons.find(key);
    return !linked && it != buttons.end() ? it->second : global;
}

void Manager::set(std::string const& key, Config config) {
    config = sanitize(config);
    if (key.empty() || linked) {
        global = config;
    } else {
        buttons[key] = config;
    }
    save();
}

void Manager::group(Config config) {
    global = sanitize(config);
    linked = true;
    save();
}

void reset() {
    auto& manager = Manager::get();
    manager.global = Config{};
    manager.linked = false;
    manager.picking = false;
    manager.buttons.clear();
    manager.saved.clear();
    manager.save();
}

std::string buttonKey(CCNode* node) {
    std::string key;
    for (auto* current = node; current && !typeinfo_cast<CCScene*>(current);
         current = current->getParent()) {
        auto id = current->getID();
        if (id.empty()) {
            unsigned index = 0;
            if (auto* parent = current->getParent(); parent && parent->getChildren()) {
                index = parent->getChildren()->indexOfObject(current);
            }
            // type names differ across compilers, so anonymous nodes use their sibling index.
            id = fmt::format("node[{}]", index);
        }
        key = fmt::format("{}/{}{}", id.size(), id, key);
    }
    return key;
}

} // namespace paimon::hover
