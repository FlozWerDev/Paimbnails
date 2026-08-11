#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Intent data scored by PaimonGuideService: localized keywords, responses,
// optional actions, and an animation.

namespace paimon::guide {

class PaimonGuideChatPopup;

// Mirrors AnimatedPaimon::Animation without coupling this data to the node.
enum class GuideAnimation {
    Talk,       // default
    Surprise,   // exclamation ("oh!")
    Point,      // point (when the action takes the user to another UI)
    Wave,       // wave (welcome)
    Sleep,      // low attention (fallback "didn't understand")
};

// Functional intents open UI or explain settings; conversational intents are chat.
enum class IntentKind {
    Functional,
    Conversational,
};

struct GuideIntent {
    std::string id;
    IntentKind kind = IntentKind::Functional;

    // Localized keywords, primarily display names and aliases.
    std::unordered_map<std::string, std::vector<std::string>> keywordsByLang;

    // Softer problem/how-to phrases; they cannot beat an exact name match.
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // Optional description tokens for coverage and tie-breaking only.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Category id for related recommendations; empty for conversational intents.
    std::string categoryId;

    // Main localized response; supports GD <cy>...</c> tags.
    std::unordered_map<std::string, std::string> responseByLang;

    // Localized variants for repeated intents; falls back to the main response.
    std::unordered_map<std::string, std::vector<std::string>> variantsByLang;

    // Follow-up text for short questions; falls back to the main response.
    std::unordered_map<std::string, std::string> followUpByLang;

    // Base score for ties between equally matched intents.
    int priority = 50;

    // Main keyword weight used when multiple intents match.
    int weight = 50;

    // Optional action for the "Take me there" button.
    std::function<void(PaimonGuideChatPopup* popup)> action = nullptr;

    GuideAnimation animation = GuideAnimation::Talk;
};

    // Related feature shown as an actionable chat chip.
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
    // Up to three related features shown as chips.
    std::vector<GuideRecommendation> recommendations;
};

}
