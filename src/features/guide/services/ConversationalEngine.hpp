#pragma once

#include <string>
#include <vector>

// pure-std engine that resolves short follow-ups against the last topic.

namespace paimon::guide {

// a topic sub-aspect matched by normalized keywords and fuzzy similarity.
struct SubTopic {
    std::string id;
    std::vector<std::string> enKeywords;
    std::vector<std::string> esKeywords;
    std::string enReply;                  // gd tags allowed.
    std::string esReply;
    std::string enHint;                   // chip label.
    std::string esHint;
};

struct TopicKnowledge {
    std::string topicId;                  // functional intent id.
    std::string enName;                   // display name.
    std::string esName;
    std::vector<SubTopic> subtopics;
    std::string enMoreReply;              // "what else?" response.
    std::string esMoreReply;
};

struct Resolution {
    bool isFollowUp = false;              // query uses the current context.
    std::string topicId;                  // empty if unresolved.
    std::string subTopicId;               // empty means the topic itself.
    bool pureReference = false;           // no new entity was named.
};

class ConversationalEngine {
public:
    // install the knowledge table once at startup.
    void setTopics(std::vector<TopicKnowledge> const& topics);

    // detect empty, pure-reference, or "what else?" queries.
    static bool looksLikeReference(std::string const& normalized,
                                   std::vector<std::string> const& contentTokens);

    // resolve against the current topic; false delegates to the normal matcher.
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
