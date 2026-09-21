#pragma once

#include "ConversationalEngine.hpp"
#include <vector>

// Hand-curated sub-aspects per feature for short follow-ups; ids kept in sync with PopupRegistry by hand.

namespace paimon::guide {

std::vector<TopicKnowledge> buildTopicKnowledge();

} // namespace paimon::guide
