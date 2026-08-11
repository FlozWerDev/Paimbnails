#include "ConversationalEngine.hpp"
#include "LightLemmatizer.hpp"

#include <algorithm>
#include <cctype>

namespace paimon::guide {

namespace {

// Tokens that refer to the current topic.
bool isPureReferenceToken(std::string const& t) {
    static std::vector<std::string> const kRef = {
        "that", "this", "it", "those", "these", "same", "there", "what", "about",
        "eso", "ese", "esa", "esto", "esta", "este", "aquel", "aquella",
        "igual", "mismo", "misma", "lo", "la", "el", "que",
    };
    return std::find(kRef.begin(), kRef.end(), t) != kRef.end();
}

// Detect "what else?"/"que mas?" prompts.
bool looksLikeMore(std::string const& normalized) {
    static std::vector<std::string> const kMore = {
        "que mas", "y que mas", "mas", "que mas hay", "algo mas", "algo mas de eso",
        "what else", "and what else", "more", "anything else", "what more",
    };
    for (auto const& m : kMore) {
        if (normalized == m) return true;
        if (normalized.find(m) != std::string::npos) return true;
    }
    return false;
}

// Action verbs signal a fresh query, not a follow-up.
bool isActionVerb(std::string const& t) {
    static std::vector<std::string> const kVerb = {
        "change", "set", "make", "open", "enable", "disable", "configure",
        "use", "want", "need", "show", "get", "put", "add", "remove", "find",
        "cambiar", "poner", "hacer", "abrir", "activar", "desactivar",
        "configurar", "usar", "quiero", "necesito", "mostrar", "conseguir",
        "agregar", "quitar", "encontrar",
    };
    return std::find(kVerb.begin(), kVerb.end(), t) != kVerb.end();
}

// Fuzzy token similarity using LightLemmatizer stems.
bool tokensSimilar(std::string const& a, std::string const& b) {
    if (a == b) return true;
    if (a.size() < 3 || b.size() < 3) return false;
    auto sa = LightLemmatizer::stem(a);
    auto sb = LightLemmatizer::stem(b);
    if (!sa.empty() && sa == sb) return true;
    // Require a three-character prefix covering half the shorter token.
    std::size_t n = std::min(a.size(), b.size());
    std::size_t match = 0;
    while (match < n && a[match] == b[match]) ++match;
    return match >= n / 2 && match >= 3;
}

}

void ConversationalEngine::setTopics(std::vector<TopicKnowledge> const& topics) {
    m_topics = topics;
}

bool ConversationalEngine::looksLikeReference(
    std::string const& /*normalized*/,
    std::vector<std::string> const& contentTokens)
{
    if (contentTokens.empty()) return true;
    for (auto const& t : contentTokens) {
        if (!isPureReferenceToken(t)) return false;
    }
    return true;
}

Resolution ConversationalEngine::resolve(
    std::string const& normalized,
    std::vector<std::string> const& contentTokens,
    std::string const& langId,
    std::string const& currentTopicId) const
{
    Resolution res;
    if (currentTopicId.empty()) return res;

    auto const* top = topic(currentTopicId);
    if (!top) return res;

    // "What else?" resolves to the topic's more answer.
    if (looksLikeMore(normalized)) {
        res.isFollowUp = true;
        res.topicId = currentTopicId;
        res.pureReference = true;
        return res;
    }

    // Pure references resolve to the current topic unless they contain an action verb.
    {
        bool hasVerb = false;
        for (auto const& t : contentTokens) {
            if (isActionVerb(t)) { hasVerb = true; break; }
        }
        if (!hasVerb && looksLikeReference(normalized, contentTokens)) {
            res.isFollowUp = true;
            res.topicId = currentTopicId;
            res.pureReference = true;
            return res;
        }
    }

    // Short (<=4 token) queries with <=2 non-reference tokens may resolve to a subtopic.
    std::size_t totalTokens = 1 + std::count(normalized.begin(), normalized.end(), ' ');
    if (contentTokens.size() <= 2 && contentTokens.size() >= 1 && totalTokens <= 4) {
        bool hasVerb = false;
        for (auto const& t : contentTokens) {
            if (isActionVerb(t)) { hasVerb = true; break; }
        }
        if (!hasVerb) {
            for (auto const& sub : top->subtopics) {
                auto const& kws = (langId == "spanish") ? sub.esKeywords : sub.enKeywords;
                for (auto const& kw : kws) {
                    for (auto const& t : contentTokens) {
                        if (isPureReferenceToken(t)) continue;
                        if (tokensSimilar(LightLemmatizer::stem(t), LightLemmatizer::stem(kw))) {
                            res.isFollowUp = true;
                            res.topicId = currentTopicId;
                            res.subTopicId = sub.id;
                            return res;
                        }
                    }
                }
            }
        }
    }

    return res;
}

TopicKnowledge const* ConversationalEngine::topic(std::string const& id) const {
    for (auto const& t : m_topics) {
        if (t.topicId == id) return &t;
    }
    return nullptr;
}

SubTopic const* ConversationalEngine::subTopic(
    std::string const& topicId, std::string const& subId) const
{
    auto const* top = topic(topicId);
    if (!top) return nullptr;
    for (auto const& s : top->subtopics) {
        if (s.id == subId) return &s;
    }
    return nullptr;
}

}
