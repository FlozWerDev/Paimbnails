#include "ModuleRegistry.hpp"
#include "../Settings.hpp"
#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::modules {

namespace {

struct Accessor {
    std::function<bool()> get;
    std::function<void(bool)> set;
};

std::unordered_map<std::string, Accessor>& accessors() {
    static std::unordered_map<std::string, Accessor> map;
    return map;
}

std::unordered_map<std::string_view, Module const*> const& byId() {
    static auto const map = [] {
        std::unordered_map<std::string_view, Module const*> out;
        for (auto const& mod : all()) out.emplace(mod.id, &mod);
        return out;
    }();
    return map;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

constexpr int kMaxParentDepth = 8;

} // namespace

Module const* find(std::string_view id) {
    auto const& map = byId();
    auto it = map.find(id);
    return it == map.end() ? nullptr : it->second;
}

Module const* findByKey(std::string_view key) {
    if (key.empty()) return nullptr;
    for (auto const& mod : all()) {
        if (key == mod.key) return &mod;
    }
    return nullptr;
}

bool isSelfEnabled(Module const& mod) {
    auto* geodeMod = Mod::get();
    if (!geodeMod) return false;

    switch (mod.backing) {
        case Backing::Setting:
            if (!geodeMod->hasSetting(mod.key)) return mod.defaultOn;
            return geodeMod->getSettingValue<bool>(mod.key);
        case Backing::Saved:
            return geodeMod->getSavedValue<bool>(mod.key, mod.defaultOn);
        case Backing::Custom: {
            auto it = accessors().find(mod.id);
            if (it == accessors().end() || !it->second.get) return mod.defaultOn;
            return it->second.get();
        }
    }
    return mod.defaultOn;
}

bool isSelfEnabled(std::string_view id) {
    auto* mod = find(id);
    return mod && isSelfEnabled(*mod);
}

bool isAvailable(Module const& mod) {
    char const* parent = mod.parent;
    for (int depth = 0; parent && *parent && depth < kMaxParentDepth; depth++) {
        auto* owner = find(parent);
        if (!owner) break;
        if (!isSelfEnabled(*owner)) return false;
        parent = owner->parent;
    }
    return true;
}

bool isEnabled(Module const& mod) {
    return isSelfEnabled(mod) && isAvailable(mod);
}

bool isEnabled(std::string_view id) {
    auto* mod = find(id);
    return mod && isEnabled(*mod);
}

void setEnabled(Module const& mod, bool enabled) {
    auto* geodeMod = Mod::get();
    if (!geodeMod) return;

    // Hot-path caches (cell height, video quality, …) snapshot settings, so any
    // module flip has to invalidate them or the change lands only on restart.
    settings::internal::invalidateSettingsCache();

    switch (mod.backing) {
        case Backing::Setting:
            if (geodeMod->hasSetting(mod.key)) {
                geodeMod->setSettingValue<bool>(mod.key, enabled);
            }
            return;
        case Backing::Saved:
            geodeMod->setSavedValue<bool>(mod.key, enabled);
            return;
        case Backing::Custom: {
            auto it = accessors().find(mod.id);
            if (it != accessors().end() && it->second.set) it->second.set(enabled);
            return;
        }
    }
}

void setEnabled(std::string_view id, bool enabled) {
    if (auto* mod = find(id)) setEnabled(*mod, enabled);
}

std::vector<Module const*> inSection(Section section) {
    std::vector<Module const*> out;
    for (auto const& mod : all()) {
        if (mod.section == section) out.push_back(&mod);
    }
    return out;
}

std::vector<Module const*> search(std::string_view query) {
    auto needle = lower(query);
    std::vector<Module const*> out;
    for (auto const& mod : all()) {
        if (needle.empty()
            || lower(mod.id).find(needle) != std::string::npos
            || lower(mod.name).find(needle) != std::string::npos
            || lower(mod.description).find(needle) != std::string::npos
            || lower(mod.group).find(needle) != std::string::npos) {
            out.push_back(&mod);
        }
    }
    return out;
}

char const* sectionId(Section section) {
    switch (section) {
        case Section::Editor:   return "editor";
        case Section::Menu:     return "menu";
        case Section::Browser:  return "browser";
        case Section::Level:    return "level";
        case Section::Info:     return "info";
        case Section::Gameplay: return "gameplay";
        case Section::Profile:  return "profile";
        case Section::Social:   return "social";
        case Section::Global:   return "global";
        case Section::System:   return "system";
    }
    return "global";
}

char const* sectionName(Section section) {
    switch (section) {
        case Section::Editor:   return "Editor";
        case Section::Menu:     return "Menu";
        case Section::Browser:  return "Levels";
        case Section::Level:    return "Level Info";
        case Section::Info:     return "Info";
        case Section::Gameplay: return "Gameplay";
        case Section::Profile:  return "Profile";
        case Section::Social:   return "Social";
        case Section::Global:   return "Interface";
        case Section::System:   return "System";
    }
    return "Interface";
}

std::vector<Section> const& sections() {
    static std::vector<Section> const kOrder = {
        Section::Global, Section::Menu, Section::Browser, Section::Level,
        Section::Info, Section::Gameplay, Section::Profile, Section::Social,
        Section::Editor, Section::System,
    };
    return kOrder;
}

void registerAccessor(std::string_view id, std::function<bool()> get,
                      std::function<void(bool)> set) {
    accessors()[std::string(id)] = {std::move(get), std::move(set)};
}

std::string featureOf(Module const& mod) {
    std::string_view id = mod.id;
    auto first = id.find('.');
    if (first == std::string_view::npos) return std::string(id);
    auto second = id.find('.', first + 1);
    if (second == std::string_view::npos) return std::string(id.substr(first + 1));
    return std::string(id.substr(first + 1, second - first - 1));
}

} // namespace paimon::modules
