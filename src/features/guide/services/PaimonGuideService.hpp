#pragma once

#include <Geode/Geode.hpp>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "GuideIntents.hpp"
#include "ConversationMemory.hpp"
#include "PopupRegistry.hpp"
#include "ConversationalEngine.hpp"
#include "GuideTopicKnowledge.hpp"
#include "GeminiClient.hpp"

// Local guide service with conversation memory and an optional Gemini mode.

namespace paimon::guide {

// Assistant uses the local matcher; Max uses Gemini.
enum class GuideMode {
    Assistant,
    Max,
};

class PaimonGuideService {
public:
    static PaimonGuideService& get();

    // Returns immediately in Assistant mode; Max completes through callback.
    using AskCallback = geode::CopyableFunction<void(GuideAnswer const&)>;
    GuideAnswer ask(std::string const& userQuery, AskCallback callback = nullptr);

    GuideMode getMode() const;
    void setMode(GuideMode mode);

    // False keeps the guide on Assistant no matter what the saved mode says.
    bool isMaxAvailable() const;

    // Up to six {chip text, query} pairs in the active language.
    std::vector<std::pair<std::string, std::string>> getSuggestions();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    std::size_t intentCount() const { return m_intents.size(); }

    // The popup clears this memory on close.
    ConversationMemory& memory() { return m_memory; }
    void resetMemory() { m_memory.clear(); }

private:
    PaimonGuideService();
    void registerIntents();

    // Lowercase, collapse spaces, and strip common ES/PT/FR accents.
    static std::string normalize(std::string s);

    // Split normalized text on whitespace and basic ASCII punctuation.
    static std::vector<std::string> tokenize(std::string const& normalized);

    // Build a localized fallback with close matches and recommendations.
    GuideAnswer makeFallback(std::vector<GuideIntent const*> const& suggestions,
                             std::string const& langId) const;

    // Build a response, varying its text on repeats.
    GuideAnswer buildAnswerFor(GuideIntent const& intent,
                               double matchScore,
                               std::string const& langId);

    // Reuse the last functional intent for a follow-up.
    GuideAnswer buildFollowUpAnswer(GuideIntent const& intent,
                                    std::string const& langId);

    // Resolve sub-topic, "more", or reference follow-ups with chips.
    GuideAnswer buildContextualAnswer(Resolution const& res,
                                      std::string const& langId);

    std::string currentTopicId() const { return m_memory.lastTopicId(); }

    // Handle category browsing; returns nullopt for normal questions.
    std::optional<GuideAnswer> tryCategoryBrowse(
        std::string const& normalized,
        std::vector<std::string> const& tokens,
        std::string const& langId) const;

    // Add same-category or runner-up recommendations.
    void attachRelatedRecommendations(
        GuideAnswer& ans,
        GuideIntent const& primary,
        GuideIntent const* runnerUp,
        std::string const& langId,
        int maxExtra = 2) const;

    // Build a recommendation from an intent ID.
    GuideRecommendation makeRecommendation(
        std::string const& intentId,
        std::string const& langId) const;

    std::vector<GuideIntent> m_intents;
    ConversationMemory m_memory;
    ConversationalEngine m_engine;
};

}
