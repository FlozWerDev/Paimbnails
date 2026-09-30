#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// intent data scored by paimonguideservice: localized keywords, responses,
// optional actions, and an animation.

namespace paimon::guide {

class PaimonGuideChatPopup;

// mirrors animatedpaimon::animation without coupling this data to the node.
enum class GuideAnimation {
    Talk,       // default
    Surprise,   // exclamation ("oh!")
    Point,      // point (when the action takes the user to another ui)
    Wave,       // wave (welcome)
    Sleep,      // low attention (fallback "didn't understand")
};

// functional intents open ui or explain settings; conversational intents are chat.
enum class IntentKind {
    Functional,
    Conversational,
};

struct GuideIntent {
    std::string id;
    IntentKind kind = IntentKind::Functional;

    // localized keywords, primarily display names and aliases.
    std::unordered_map<std::string, std::vector<std::string>> keywordsByLang;

    // softer problem/how-to phrases; they cannot beat an exact name match.
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // optional description tokens for coverage and tie-breaking only.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // category id for related recommendations; empty for conversational intents.
    std::string categoryId;

    // main localized response; supports gd <cy>...</c> tags.
    std::unordered_map<std::string, std::string> responseByLang;

    // localized variants for repeated intents; falls back to the main response.
    std::unordered_map<std::string, std::vector<std::string>> variantsByLang;

    // main keyword weight used when multiple intents match.
    int weight = 50;

    // optional action for the "take me there" button.
    std::function<void(PaimonGuideChatPopup* popup)> action = nullptr;

    GuideAnimation animation = GuideAnimation::Talk;
};

    // related feature shown as an actionable chat chip.
struct GuideRecommendation {
    std::string intentId;
    std::string label;
    std::function<void(PaimonGuideChatPopup* popup)> action;
};

struct GuideAnswer {
    std::string message;
    std::function<void(PaimonGuideChatPopup* popup)> action;
    GuideAnimation animation = GuideAnimation::Talk;
    bool found = true;
    std::string matchedIntentId;
    // up to three related features shown as chips.
    std::vector<GuideRecommendation> recommendations;
};

}
