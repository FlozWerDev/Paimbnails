#include "ModuleRegistry.hpp"
#include "../Settings.hpp"
#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <atomic>
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

// isEnabled by id runs in per-frame hooks; slots cache it by position in all().
// Relaxed atomics: racing readers just compute twice, cheaper than locking.
enum : uint8_t { kCacheUnknown = 0, kCacheOff = 1, kCacheOn = 2 };

std::vector<std::atomic<uint8_t>>& enabledSlots() {
    static std::vector<std::atomic<uint8_t>> slots(all().size());
    return slots;
}

std::atomic<uint64_t>& cachedVersion() {
    static std::atomic<uint64_t> version{UINT64_MAX};
    return version;
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
    if (!mod) return false;

    // Custom modules read a manager's config from memory, which moves without touching
    // the settings version; no parent is Custom, so the cached chain stays exact.
    if (mod->backing == Backing::Custom) return isEnabled(*mod);

    uint64_t const version = settings::internal::g_settingsVersion.load(std::memory_order_relaxed);
    auto& slots = enabledSlots();
    if (cachedVersion().load(std::memory_order_relaxed) != version) {
        cachedVersion().store(version, std::memory_order_relaxed);
        for (auto& slot : slots) slot.store(kCacheUnknown, std::memory_order_relaxed);
    }

    // find() hands back a pointer into all(), so the offset is the slot index.
    auto const index = static_cast<size_t>(mod - all().data());
    if (auto cached = slots[index].load(std::memory_order_relaxed); cached != kCacheUnknown) {
        return cached == kCacheOn;
    }

    bool const enabled = isEnabled(*mod);
    slots[index].store(enabled ? kCacheOn : kCacheOff, std::memory_order_relaxed);
    return enabled;
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

std::vector<Module const*> search(std::string_view query) {
    auto needle = lower(query);
    std::vector<Module const*> out;
    auto hit = [&needle](char const* field) {
        return field && lower(field).find(needle) != std::string::npos;
    };
    for (auto const& mod : all()) {
        // Se busca tambien sobre los textos traducidos: la lista muestra el
        // nombre localizado, asi que "dinamico" tiene que encontrar el modulo
        // que en ingles se llama "Dynamic Volume".
        if (needle.empty()
            || hit(mod.id)
            || hit(mod.name)
            || hit(mod.description)
            || hit(mod.group)
            || hit(localizedName(mod))
            || hit(localizedDescription(mod))
            || hit(localizedGroup(mod.group))) {
            out.push_back(&mod);
        }
    }
    return out;
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

} // namespace paimon::modules
