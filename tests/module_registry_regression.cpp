#include "../src/core/Settings.hpp"
#include "../src/core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::modules {

char const* localizedName(Module const& mod) { return mod.name; }
char const* localizedDescription(Module const& mod) { return mod.description; }
char const* localizedSection(Section section) { return sectionName(section); }
char const* localizedGroup(char const* group) { return group; }

} // namespace paimon::modules

int main() {
    using namespace paimon::modules;

    geode::Mod mod;
    geode::Mod::set(&mod);
    std::unordered_map<std::string, bool> customValues;
    auto const& catalog = all();

    for (auto const& module : catalog) {
        switch (module.backing) {
            case Backing::Setting:
                mod.setSettingValue<bool>(module.key, module.defaultOn);
                break;
            case Backing::Saved:
                mod.setSavedValue<bool>(module.key, module.defaultOn);
                break;
            case Backing::Custom: {
                std::string const id = module.id;
                customValues.emplace(id, module.defaultOn);
                registerAccessor(id,
                    [&customValues, id] { return customValues.at(id); },
                    [&customValues, id](bool enabled) { customValues.at(id) = enabled; });
                break;
            }
        }
    }

    assert(!catalog.empty());
    for (auto const& module : catalog) {
        assert(find(module.id) == &module);
        assert(isSelfEnabled(module) == module.defaultOn);

        setEnabled(module, false);
        assert(!isSelfEnabled(module));
        setEnabled(module, true);
        assert(isSelfEnabled(module));

        if (module.backing != Backing::Custom) {
            auto const before = paimon::settings::internal::g_settingsVersion.load();
            setEnabled(module, false);
            auto const after = paimon::settings::internal::g_settingsVersion.load();
            assert(after > before);
            assert(!isSelfEnabled(module));
            setEnabled(module, true);
        }
    }

    auto* infoSuite = find("paimbnails.infosuite.info");
    auto* extendedInfo = find("paimbnails.extendedinfo.info");
    assert(infoSuite && extendedInfo);
    assert(isSelfEnabled(*extendedInfo));
    assert(isEnabled(*extendedInfo));
    setEnabled(*infoSuite, false);
    assert(!isEnabled(*extendedInfo));
    assert(!isAvailable(*extendedInfo));
    assert(blockingParent(*extendedInfo) == infoSuite);
    assert(isSelfEnabled(*extendedInfo));
    setEnabled(*infoSuite, true);
    assert(isEnabled(*extendedInfo));

    auto* cursor = find("paimbnails.cursor.global");
    auto* cursorTransitions = find("paimbnails.cursortransition.global");
    assert(cursor && cursorTransitions);
    setEnabled(*cursor, false);
    assert(isSelfEnabled(*cursorTransitions));
    assert(!isEnabled(*cursorTransitions));
    assert(blockingParent(*cursorTransitions) == cursor);
    setEnabled(*cursor, true);
    assert(isEnabled(*cursorTransitions));

    auto* defaultSetting = find("paimbnails.smoothui.global");
    assert(defaultSetting);
    mod.eraseSetting(defaultSetting->key);
    assert(isSelfEnabled(*defaultSetting) == defaultSetting->defaultOn);
    mod.setSettingValue<bool>(defaultSetting->key, defaultSetting->defaultOn);

    auto* defaultSaved = find("paimbnails.menuloop.menu");
    assert(defaultSaved);
    mod.eraseSaved(defaultSaved->key);
    assert(isSelfEnabled(*defaultSaved) == defaultSaved->defaultOn);
    mod.setSavedValue<bool>(defaultSaved->key, defaultSaved->defaultOn);

    auto matches = search("pAiMoN");
    assert(!matches.empty());
    assert(find("paimbnails.does-not-exist") == nullptr);
    assert(!isEnabled("paimbnails.does-not-exist"));
    assert(sections().size() == 10);
    assert(std::string(sectionName(Section::Global)) == "Interface");

    geode::Mod::set(nullptr);
    assert(!isSelfEnabled(*infoSuite));
    setEnabled(*infoSuite, false); // no active Geode Mod: no write is attempted

    std::cout << "Module registry checks passed for " << catalog.size()
              << " entries.\n";
}
