#pragma once

// Central module registry: stable canonical ids (paimbnails.<feature>.<section>)
// over whatever storage each feature already used, so nothing is migrated.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::modules {

// Third segment of the id: where in the game the module does its work.
enum class Section {
    Editor,
    Menu,
    Browser,
    Level,
    Info,
    Gameplay,
    Profile,
    Social,
    Global,
    System,
};

enum class Backing {
    Setting,  // mod.json bool setting
    Saved,    // Mod::getSavedValue<bool>
    Custom,   // accessor registered by the feature
};

struct Module {
    char const* id;
    char const* name;
    char const* description;
    char const* key;      // setting / saved key, empty for Custom
    char const* parent;   // id of the master module, empty if none
    char const* group;    // subgroup inside the section
    Section section;
    Backing backing;
    bool defaultOn;
};

std::vector<Module> const& all();

Module const* find(std::string_view id);

// Own toggle only, ignoring parents.
bool isSelfEnabled(Module const& mod);
bool isSelfEnabled(std::string_view id);

// Own toggle && every parent up the chain.
bool isEnabled(Module const& mod);
bool isEnabled(std::string_view id);

void setEnabled(Module const& mod, bool enabled);
void setEnabled(std::string_view id, bool enabled);

// Modules whose parent chain is satisfied can be toggled by the user.
bool isAvailable(Module const& mod);

std::vector<Module const*> search(std::string_view query);

char const* sectionName(Section section);
std::vector<Section> const& sections();

// Backing::Custom modules bind their own storage (manager configs).
void registerAccessor(std::string_view id, std::function<bool()> get,
                      std::function<void(bool)> set);

// Display name/description in the current language; English is used for the
// "request" modules and when the mod is not running in Spanish.
char const* localizedName(Module const& mod);
char const* localizedDescription(Module const& mod);

// Display section name and module group label in the current language.
char const* localizedSection(Section section);
char const* localizedGroup(char const* group);

} // namespace paimon::modules
