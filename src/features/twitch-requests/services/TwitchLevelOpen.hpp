#pragma once

#include <cstddef>
#include <optional>

namespace paimon::twitch {

// replacescene: true between requests (swaps the scene so back returns to
// the list), false on entry from the queue.
void openRequestedLevel(int levelID, bool replaceScene);

// A request is reviewed only after its level opens successfully.
void playRequestAt(size_t index, bool replaceScene);

// queue index of that id, if still there.
std::optional<size_t> indexOfRequest(int levelID);
std::optional<size_t> adjacentRequestIndex(int levelID, bool forward);

} // namespace paimon::twitch
