#include "ModuleRegistry.hpp"
#include "../Settings.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <string_view>
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

// isenabled by id runs in per-frame hooks; version-tagged atomic slots cache it
// without linear clears or cross-thread invalidation races.
std::vector<std::atomic<uint64_t>>& enabledSlots() {
    static std::vector<std::atomic<uint64_t>> slots(all().size());
    return slots;
}

std::string lower(std::string_view text) {
    return geode::utils::string::toLower(std::string(text));
}

bool containsFolded(char const* haystack, std::string const& needle) {
    if (!haystack) return false;
    if (needle.empty()) return true;
    std::string_view hay(haystack);
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }) != hay.end();
}

constexpr int kMaxParentDepth = 8;

} // namespace

Module const* find(std::string_view id) {
    auto const& map = byId();
    auto it = map.find(id);
    return it == map.end() ? nullptr : it->second;
}

bool isSupportedOnThisPlatform(Module const& mod) {
#if !defined(GEODE_IS_DESKTOP)
    std::string_view const id(mod.id ? mod.id : "");
    if (id == "paimbnails.cursor.global" ||
        id == "paimbnails.cursortransition.global" ||
        id == "paimbnails.cursorclick.global" ||
        id == "paimbnails.smoothscroll.global" ||
        id == "paimbnails.inputscroll.editor" ||
        id == "paimbnails.streamoverlay.menu" ||
        id == "paimbnails.discordrpc.system") {
        return false;
    }
    if (mod.backing == Backing::Setting && mod.key && *mod.key) {
        if (auto* geodeMod = Mod::get(); geodeMod && !geodeMod->hasSetting(mod.key)) {
            return false;
        }
    }
    if (mod.parent && *mod.parent) {
        if (auto* parent = find(mod.parent)) {
            return isSupportedOnThisPlatform(*parent);
        }
    }
#else
    (void)mod;
#endif
    return true;
}

bool isSelfEnabled(Module const& mod) {
    if (!isSupportedOnThisPlatform(mod)) return false;
    auto* geodeMod = Mod::get();
    if (!geodeMod) return false;

    switch (mod.backing) {
        case Backing::Setting:
            if (!geodeMod->hasSetting(mod.key)) return false;
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

Module const* blockingParent(Module const& mod) {
    char const* parent = mod.parent;
    for (int depth = 0; parent && *parent && depth < kMaxParentDepth; depth++) {
        auto* owner = find(parent);
        if (!owner) break;
        if (!isSelfEnabled(*owner)) return owner;
        parent = owner->parent;
    }
    return nullptr;
}

bool isAvailable(Module const& mod) {
    return blockingParent(mod) == nullptr;
}

bool isEnabled(Module const& mod) {
    return isSelfEnabled(mod) && isAvailable(mod);
}

bool isEnabled(std::string_view id) {
    auto* mod = find(id);
    if (!mod) return false;

    // custom modules read a manager's config from memory, which moves without touching
    // the settings version; no parent is custom, so the cached chain stays exact.
    if (mod->backing == Backing::Custom) return isEnabled(*mod);

    uint64_t const version = settings::internal::g_settingsVersion.load(std::memory_order_relaxed);
    auto& slots = enabledSlots();

    // find() hands back a pointer into all(), so the offset is the slot index.
    // the slots vector is sized once from all(); never index it unchecked.
    auto const index = static_cast<size_t>(mod - all().data());
    if (index >= slots.size()) return isEnabled(*mod);

    uint64_t const versionBits = (version + 1ull) << 1;
    uint64_t const packed = slots[index].load(std::memory_order_relaxed);
    if ((packed & ~1ull) == versionBits) {
        return (packed & 1ull) != 0;
    }

    bool const enabled = isEnabled(*mod);
    slots[index].store(versionBits | (enabled ? 1ull : 0ull), std::memory_order_relaxed);
    return enabled;
}

void setEnabled(Module const& mod, bool enabled) {
    auto* geodeMod = Mod::get();
    if (!geodeMod) return;

    // hot-path caches (cell height, video quality, …) snapshot settings, so any
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
    if (needle.empty()) {
        out.reserve(all().size());
        for (auto const& mod : all()) {
            if (isSupportedOnThisPlatform(mod)) out.push_back(&mod);
        }
        return out;
    }
    out.reserve(16);
    auto hit = [&needle](char const* field) {
        return containsFolded(field, needle);
    };
    for (auto const& mod : all()) {
        if (!isSupportedOnThisPlatform(mod)) continue;
        // the list shows localized names, so search those too.
        if (hit(mod.id)
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
