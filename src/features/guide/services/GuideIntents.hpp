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
    // Primary signal: display names + aliases. Strong tiers (exact/compound).
    std::unordered_map<std::string, std::vector<std::string>> keywordsByLang;

    // Language -> problem / "how do I" phrases that should route here with a
    // softer score cap so they never beat an exact name match on another intent.
    // Example: "no se ven miniaturas" -> thumbnail-settings.
    std::unordered_map<std::string, std::vector<std::string>> searchPhrasesByLang;

    // Optional description text tokens used only as coverage/desempate (not for
    // qualification alone). Filled from PopupEntry descriptions when available.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Logical category id string for related recommendations (mirrors PopupCategory).
    // Empty = none / conversational.
    std::string categoryId;

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

// Actionable related feature shown as a dynamic chip under the chat.
struct GuideRecommendation {
    std::string intentId;
    std::string label; // short display name for the chip
    std::function<void(PaimonGuideChatPopup* popup)> action;
};

struct GuideAnswer {
    std::string message;            // already translated to the active language
    std::function<void(PaimonGuideChatPopup* popup)> action;
    GuideAnimation animation = GuideAnimation::Talk;
    bool found = true;              // false => generic fallback response
    std::string matchedIntentId;    // useful for logs
    // 0..3 related features the UI can show as chips (open or re-query).
    std::vector<GuideRecommendation> recommendations;
};

} // namespace paimon::guide
