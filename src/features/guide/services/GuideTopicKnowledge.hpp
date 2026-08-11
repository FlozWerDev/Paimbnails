#pragma once

#include "ConversationalEngine.hpp"
#include <vector>

// Hand-curated knowledge table for the conversational engine: the sub-aspects
// of each feature users ask about with short follow-ups ("y el color?") and
// ready answers. Kept in sync with PopupRegistry ids by hand (same as the
// test fixture). Entries without a topic here simply fall back to the normal
// matcher — the table only makes supported topics conversational.

namespace paimon::guide {

std::vector<TopicKnowledge> buildTopicKnowledge();

} // namespace paimon::guide
