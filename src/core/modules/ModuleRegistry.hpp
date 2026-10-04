#pragma once

// stable ids over existing storage; nothing is migrated.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::modules {

// third segment of the id: where in the game the module does its work.
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
    Saved,    // mod::getsavedvalue<bool>
    Custom,   // accessor registered by the feature
};

struct Module {
    char const* id;
    char const* name;
    char const* description;
    char const* key;      // setting / saved key, empty for custom
    char const* parent;   // id of the master module, empty if none
    char const* group;    // subgroup inside the section
    Section section;
    Backing backing;
    bool defaultOn;
};

std::vector<Module> const& all();

Module const* find(std::string_view id);

// own toggle only, ignoring parents.
bool isSelfEnabled(Module const& mod);
bool isSelfEnabled(std::string_view id);

// own toggle && every parent up the chain.
bool isEnabled(Module const& mod);
bool isEnabled(std::string_view id);

void setEnabled(Module const& mod, bool enabled);
void setEnabled(std::string_view id, bool enabled);

// modules whose parent chain is satisfied can be toggled by the user.
bool isAvailable(Module const& mod);
Module const* blockingParent(Module const& mod);

std::vector<Module const*> search(std::string_view query);

char const* sectionName(Section section);
std::vector<Section> const& sections();

// backing::custom modules bind their own storage (manager configs).
void registerAccessor(std::string_view id, std::function<bool()> get,
                      std::function<void(bool)> set);

// localized display name; english for requests modules and non-spanish.
char const* localizedName(Module const& mod);
char const* localizedDescription(Module const& mod);

// display section name and module group label in the current language.
char const* localizedSection(Section section);
char const* localizedGroup(char const* group);

} // namespace paimon::modules
