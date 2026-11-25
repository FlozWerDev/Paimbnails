#pragma once

#include <matjson.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::editormcp {

struct TriggerDef {
    int id;
    char const* name;
    char const* category;
    char const* description;
    char const* params;
};

struct CuratedObjectDef {
    int id;
    char const* name;
    char const* category;
    char const* notes;
};

struct PropertyKeyDef {
    int key;
    char const* name;
    char const* type;
    char const* description;
};

std::vector<TriggerDef> const& triggerCatalog();
std::vector<CuratedObjectDef> const& curatedObjectCatalog();
std::vector<PropertyKeyDef> const& propertyKeyCatalog();

// Resolves a trigger alias or numeric string to its GD object ID (0 if unknown).
int resolveTriggerId(std::string_view nameOrId);

// Builds comprehensive JSON or markdown documentation for AI clients.
matjson::Value buildReferenceJson(std::string_view topic, std::string_view filter = "");
std::string buildReferenceMarkdown(std::string_view topic);
// Builds a compact, strictly technical trigger specification + key-code reference block for prompts/instructions.
std::string buildTriggerCatalogPromptBlock();

// Searches curated catalog + live ObjectToolbox::sharedState()->m_allKeys (main thread when live=true).
matjson::Value searchObjectCatalog(std::string_view query, std::string_view category, int limit, bool queryGameToolbox);

} // namespace paimon::editormcp
