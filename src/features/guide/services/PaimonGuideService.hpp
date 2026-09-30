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

// local guide service with conversation memory and an optional gemini mode.

namespace paimon::guide {

// assistant uses the local matcher; max uses gemini.
enum class GuideMode {
    Assistant,
    Max,
};

class PaimonGuideService {
public:
    static PaimonGuideService& get();

    // returns immediately in assistant mode; max completes through callback.
    using AskCallback = geode::CopyableFunction<void(GuideAnswer const&)>;
    GuideAnswer ask(std::string const& userQuery, AskCallback callback = nullptr);

    GuideMode getMode() const;
    void setMode(GuideMode mode);

    // false keeps the guide on assistant no matter what the saved mode says.
    bool isMaxAvailable() const;

    // up to six {chip text, query} pairs in the active language.
    std::vector<std::pair<std::string, std::string>> getSuggestions();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    std::size_t intentCount() const { return m_intents.size(); }

    // the popup clears this memory on close.
    ConversationMemory& memory() { return m_memory; }
    void resetMemory() { m_memory.clear(); }

private:
    PaimonGuideService();
    void registerIntents();

    // lowercase, collapse spaces, and strip common es/pt/fr accents.
    static std::string normalize(std::string s);

    // split normalized text on whitespace and basic ascii punctuation.
    static std::vector<std::string> tokenize(std::string const& normalized);

    // build a localized fallback with close matches and recommendations.
    GuideAnswer makeFallback(std::vector<GuideIntent const*> const& suggestions,
                             std::string const& langId) const;

    // build a response, varying its text on repeats.
    GuideAnswer buildAnswerFor(GuideIntent const& intent,
                               std::string const& langId);

    // reuse the last functional intent for a follow-up.
    GuideAnswer buildFollowUpAnswer(GuideIntent const& intent,
                                    std::string const& langId);

    // resolve sub-topic, "more", or reference follow-ups with chips.
    GuideAnswer buildContextualAnswer(Resolution const& res,
                                      std::string const& langId);

    std::string currentTopicId() const { return m_memory.lastTopicId(); }

    // handle category browsing; returns nullopt for normal questions.
    std::optional<GuideAnswer> tryCategoryBrowse(
        std::string const& normalized,
        std::vector<std::string> const& tokens,
        std::string const& langId) const;

    // add same-category or runner-up recommendations.
    void attachRelatedRecommendations(
        GuideAnswer& ans,
        GuideIntent const& primary,
        GuideIntent const* runnerUp,
        std::string const& langId,
        int maxExtra = 2) const;

    // build a recommendation from an intent id.
    GuideRecommendation makeRecommendation(
        std::string const& intentId,
        std::string const& langId) const;

    std::vector<GuideIntent> m_intents;
    ConversationMemory m_memory;
    ConversationalEngine m_engine;
};

}
