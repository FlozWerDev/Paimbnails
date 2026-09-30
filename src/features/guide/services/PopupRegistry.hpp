#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// registry of popup metadata used as paimon's knowledge base. entries provide
// localized names/aliases, category, weight, description, and optional open().

namespace paimon::guide {

class PaimonGuideChatPopup;

// logical categories used for broad queries and recommendations.
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

    // localized popup title; english is the fallback.
    std::unordered_map<std::string, std::string> displayNameByLang;

    // localized aliases not present in the title.
    std::unordered_map<std::string, std::vector<std::string>> aliasesByLang;

    // softer problem/how-to phrases.
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // localized response shown before opening.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // optional opener; null means description-only.
    std::function<void(PaimonGuideChatPopup* popup)> open = nullptr;

    GuideAnimation animation = GuideAnimation::Point;
};

// stable category id used by guideintent.
char const* categoryIdString(PopupCategory cat);

PopupCategory categoryFromId(std::string const& id);

std::string categoryDisplayName(PopupCategory cat, std::string const& langId);

class PopupRegistry {
public:
    static PopupRegistry& get();

    std::vector<PopupEntry> const& entries() const { return m_entries; }

    // rebuild entries; all languages are preloaded.
    void rebuild();

    // convert an entry to the guideintent consumed by paigoritv1.
    static GuideIntent toIntent(PopupEntry const& entry);

    // localized display name, then english, then a prettified id.
    std::string displayNameFor(std::string const& id, std::string const& langId) const;

    // look up a full entry by id (nullptr if missing).
    PopupEntry const* findById(std::string const& id) const;

    // entries in a category, highest weight first.
    std::vector<PopupEntry const*> entriesInCategory(PopupCategory cat) const;

private:
    PopupRegistry();
    void registerAll();

    std::vector<PopupEntry> m_entries;
};

}
