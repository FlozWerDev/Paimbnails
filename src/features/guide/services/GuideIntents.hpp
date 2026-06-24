#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Data structures for the intents the Paimon guide understands: id, per-language
// keywords, per-language response, optional action, and an animation.
// PaimonGuideService scores these against a query and returns the best match.

namespace paimon::guide {

class PaimonGuideChatPopup; // forward declaration

// Animations Paimon plays when responding; mirrors AnimatedPaimon::Animation
// to avoid coupling intents to the graphics node.
enum class GuideAnimation {
    Talk,       // default
    Surprise,   // exclamation ("oh!")
    Point,      // point (when the action takes the user to another UI)
    Wave,       // wave (welcome)
    Sleep,      // low attention (fallback "didn't understand")
};

// Intent type. Functional opens popups or explains settings; Conversational is
// chat. The matcher uses a stricter threshold for Conversational intents.
enum class IntentKind {
    Functional,
    Conversational,
};

struct GuideIntent {
    std::string id;
    IntentKind kind = IntentKind::Functional;

    // Language -> synonyms (lowercase, ASCII). Any match scores the intent.
    std::unordered_map<std::string, std::vector<std::string>> keywordsByLang;

    // Main response per language (key = Localization id). Supports GD <cy>...</c> tags.
    std::unordered_map<std::string, std::string> responseByLang;

    // Response variants for repeated intents (language -> variants); falls back to the main response.
    std::unordered_map<std::string, std::vector<std::string>> variantsByLang;

    // Follow-up message for short questions after this intent (per language); falls back to the main response.
    std::unordered_map<std::string, std::string> followUpByLang;

    // Base score, used to break ties when intents match the same number of keywords.
    int priority = 50;

    // Weight of this intent's main keyword; the higher-weight keyword wins when
    // multiple intents match a query. Suggested 1-200 (50 generic .. 150 unique). Default 50.
    int weight = 50;

    // Optional action run when the user taps "Take me there"; receives the current popup.
    std::function<void(PaimonGuideChatPopup* popup)> action = nullptr;

    GuideAnimation animation = GuideAnimation::Talk;
};

struct GuideAnswer {
    std::string message;            // already translated to the active language
    std::function<void(PaimonGuideChatPopup* popup)> action;
    GuideAnimation animation = GuideAnimation::Talk;
    bool found = true;              // false => generic fallback response
    std::string matchedIntentId;    // useful for logs
};

} // namespace paimon::guide
