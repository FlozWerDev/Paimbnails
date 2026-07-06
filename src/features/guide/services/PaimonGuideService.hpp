#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <utility>

#include "GuideIntents.hpp"
#include "ConversationMemory.hpp"

// Singleton that owns all GuideIntents and answers user questions fully locally
// (no network, no external AI). Keeps a ConversationMemory for contextual
// answers: detects repeats and short follow-ups and avoids repeating itself.
// Also exposes getSuggestions(), isEnabled()/setEnabled(), and resetMemory().

namespace paimon::guide {

class PaimonGuideService {
public:
    static PaimonGuideService& get();

    // Main entry: called when the user hits Send. Empty/unmatched queries return a found=false fallback.
    GuideAnswer ask(std::string const& userQuery);

    // Up to 6 suggestions in the active language: {chip text, query to inject}.
    std::vector<std::pair<std::string, std::string>> getSuggestions();

    // Saved-value "guide-enabled".
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // For tests / internal debug.
    std::size_t intentCount() const { return m_intents.size(); }

    // Conversation memory; the popup clears it on close.
    ConversationMemory& memory() { return m_memory; }
    void resetMemory() { m_memory.clear(); }

private:
    PaimonGuideService();
    void registerIntents();

    // Normalize a string: lowercase, collapse spaces, strip common ES/PT/FR accents (not full Unicode).
    static std::string normalize(std::string s);

    // Tokenize the normalized string on whitespace and basic ASCII punctuation.
    static std::vector<std::string> tokenize(std::string const& normalized);

    // Build a "didn't understand" GuideAnswer in the active language. When the
    // matcher found close-but-unqualified candidates, names them as suggestions.
    GuideAnswer makeFallback(std::vector<GuideIntent const*> const& suggestions,
                             std::string const& langId) const;

    // Build a response for an intent, varying the message on repeats (variants).
    GuideAnswer buildAnswerFor(GuideIntent const& intent,
                               double matchScore,
                               std::string const& langId);

    // Build a follow-up response reusing the last functional intent.
    GuideAnswer buildFollowUpAnswer(GuideIntent const& intent,
                                    std::string const& langId);

    std::vector<GuideIntent> m_intents;
    ConversationMemory m_memory;
};

} // namespace paimon::guide
