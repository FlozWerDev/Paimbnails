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
};

struct PopupEntry {
    std::string id;
    PopupCategory category = PopupCategory::None;
    int weight = 80;

    // Real popup title per language; the active language is used for matching, falling back to "english".
    std::unordered_map<std::string, std::string> displayNameByLang;

    // Aliases/synonyms the user might use that aren't in the title (e.g. "pfp" for Profile Photo Editor), per language.
    std::unordered_map<std::string, std::vector<std::string>> aliasesByLang;

    // Short message Paimon says before taking you there, per language.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Lambda that opens the popup. If null, Paimon only describes it.
    std::function<void(PaimonGuideChatPopup* popup)> open = nullptr;

    GuideAnimation animation = GuideAnimation::Point;
};

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

private:
    PopupRegistry();
    void registerAll();

    std::vector<PopupEntry> m_entries;
};

} // namespace paimon::guide
