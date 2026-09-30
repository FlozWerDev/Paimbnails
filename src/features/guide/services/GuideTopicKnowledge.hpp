#pragma once

#include "ConversationalEngine.hpp"
#include <vector>

// hand-curated sub-aspects per feature for short follow-ups; ids kept in sync with popupregistry by hand.

namespace paimon::guide {

std::vector<TopicKnowledge> buildTopicKnowledge();

} // namespace paimon::guide
