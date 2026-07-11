#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// Central registry of the mod's popups/layers, used as Paimon's knowledge base.
// Paimon learns what exists from the popups' real titles (displayName) instead
// of hand-maintained keyword lists. Each entry has an id, per-language
// displayName + aliases, a category, a weight (for disambiguation), an open()
// lambda, and a description. So "profile background" matches the popup whose
// displayName fits best and opens it; categories let broad queries land on the
// main popup of that group.

namespace paimon::guide {

class PaimonGuideChatPopup;

// Logical popup categories, so generic queries ("music") route to the main popup of that category.
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
    Editor,   // editor tools: history, filters, collab, color picker, rotate
    Visuals,  // effects, shaders, scroll, slider, icons, textures, score cells
};

struct PopupEntry {
    std::string id;
    PopupCategory category = PopupCategory::None;
    int weight = 80;

    // Real popup title per language; the active language is used for matching, falling back to "english".
    std::unordered_map<std::string, std::string> displayNameByLang;

    // Aliases/synonyms the user might use that aren't in the title (e.g. "pfp" for Profile Photo Editor), per language.
    std::unordered_map<std::string, std::vector<std::string>> aliasesByLang;

    // Problem / natural "how do I" phrases (softer match than names/aliases).
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // Short message Paimon says before taking you there, per language.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Lambda that opens the popup. If null, Paimon only describes it.
    std::function<void(PaimonGuideChatPopup* popup)> open = nullptr;

    GuideAnimation animation = GuideAnimation::Point;
};

// Stable string id for a PopupCategory (used on GuideIntent.categoryId).
char const* categoryIdString(PopupCategory cat);

// Parse a category id string back to enum (None if unknown).
PopupCategory categoryFromId(std::string const& id);

// Human label for a category in the given language.
std::string categoryDisplayName(PopupCategory cat, std::string const& langId);

class PopupRegistry {
public:
    static PopupRegistry& get();

    // All registered entries (read-only).
    std::vector<PopupEntry> const& entries() const { return m_entries; }

    // Rebuild the registry. Called when the service starts; no need to rebuild on
    // runtime language change since displayNameByLang preloads all languages.
    void rebuild();

    // Convert an entry into an equivalent GuideIntent for PaigoritV1::run. Keywords
    // are built as [displayName + aliases] per language; weight/category drive the final score.
    static GuideIntent toIntent(PopupEntry const& entry);

    // Human-readable display name for an intent/entry id in the given language
    // (falls back to english, then a prettified id). Used to name alternatives
    // in "did you mean ...?" answers.
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

} // namespace paimon::guide
