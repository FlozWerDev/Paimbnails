#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// Registry of popup metadata used as Paimon's knowledge base. Entries provide
// localized names/aliases, category, weight, description, and optional open().

namespace paimon::guide {

class PaimonGuideChatPopup;

// Logical categories used for broad queries and recommendations.
enum class PopupCategory {
    None,
    Background,
    Music,
    Profile,
    Capture,
    Cursor,
    Pet,
    Discord,
    Forum,
    Emote,
    Transition,
    Layout,
    Volume,
    Cache,
    Update,
    Language,
    QuickHub,
    Thumbnail,
    Help,
    Editor,
    Visuals,
};

struct PopupEntry {
    std::string id;
    PopupCategory category = PopupCategory::None;
    int weight = 80;

    // Localized popup title; English is the fallback.
    std::unordered_map<std::string, std::string> displayNameByLang;

    // Localized aliases not present in the title.
    std::unordered_map<std::string, std::vector<std::string>> aliasesByLang;

    // Softer problem/how-to phrases.
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // Localized response shown before opening.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Optional opener; null means description-only.
    std::function<void(PaimonGuideChatPopup* popup)> open = nullptr;

    GuideAnimation animation = GuideAnimation::Point;
};

    // Stable category id used by GuideIntent.
char const* categoryIdString(PopupCategory cat);

PopupCategory categoryFromId(std::string const& id);

std::string categoryDisplayName(PopupCategory cat, std::string const& langId);

class PopupRegistry {
public:
    static PopupRegistry& get();

    std::vector<PopupEntry> const& entries() const { return m_entries; }

    // Rebuild entries; all languages are preloaded.
    void rebuild();

    // Convert an entry to the GuideIntent consumed by PaigoritV1.
    static GuideIntent toIntent(PopupEntry const& entry);

    // Localized display name, then English, then a prettified id.
    std::string displayNameFor(std::string const& id, std::string const& langId) const;

    // Look up a full entry by id (nullptr if missing).
    PopupEntry const* findById(std::string const& id) const;

    // Entries in a category, highest weight first.
    std::vector<PopupEntry const*> entriesInCategory(PopupCategory cat) const;

    // Highest-weight entry in a category (nullptr if none).
    PopupEntry const* categoryLead(PopupCategory cat) const;

private:
    PopupRegistry();
    void registerAll();

    std::vector<PopupEntry> m_entries;
};

}
