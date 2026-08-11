#pragma once

#include <string>
#include <vector>

// Pure-std engine that resolves short follow-ups against the last topic.

namespace paimon::guide {

// A topic sub-aspect matched by normalized keywords and fuzzy similarity.
struct SubTopic {
    std::string id;
    std::vector<std::string> enKeywords;
    std::vector<std::string> esKeywords;
    std::string enReply;                  // GD tags allowed.
    std::string esReply;
    std::string enHint;                   // Chip label.
    std::string esHint;
};

struct TopicKnowledge {
    std::string topicId;                  // Functional intent ID.
    std::string enName;                   // Display name.
    std::string esName;
    std::vector<SubTopic> subtopics;
    std::string enMoreReply;              // "What else?" response.
    std::string esMoreReply;
};

struct Resolution {
    bool isFollowUp = false;              // Query uses the current context.
    std::string topicId;                  // Empty if unresolved.
    std::string subTopicId;               // Empty means the topic itself.
    bool pureReference = false;           // No new entity was named.
};

class ConversationalEngine {
public:
    // Install the knowledge table once at startup.
    void setTopics(std::vector<TopicKnowledge> const& topics);

    // Detect empty, pure-reference, or "what else?" queries.
    static bool looksLikeReference(std::string const& normalized,
                                   std::vector<std::string> const& contentTokens);

    // Resolve against the current topic; false delegates to the normal matcher.
    Resolution resolve(std::string const& normalized,
                       std::vector<std::string> const& contentTokens,
                       std::string const& langId,
                       std::string const& currentTopicId) const;

    TopicKnowledge const* topic(std::string const& id) const;
    SubTopic const* subTopic(std::string const& topicId, std::string const& subId) const;

private:
    std::vector<TopicKnowledge> m_topics;
};

}
