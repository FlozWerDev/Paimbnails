#pragma once

// Single source of truth for editor module toggles (Modules layer + settings panel).

#include <string>
#include <vector>

namespace paimon::editor {

struct EditorModuleInfo {
    char const* key;          // mod.json bool key
    char const* name;         // display name
    char const* description;  // short description
    char const* category;     // UI category under Editor
    bool suiteGated;          // true = editor-mod-* (needs suite master)
    bool defaultOn;
};

// Full catalog of master toggles exposed in Modules / settings.
std::vector<EditorModuleInfo> const& getEditorModuleCatalog();

// Convenience: only keys safe for PaimonModulesLayer (bool settings that exist).
std::vector<EditorModuleInfo> getModulesLayerEntries();

} // namespace paimon::editor
