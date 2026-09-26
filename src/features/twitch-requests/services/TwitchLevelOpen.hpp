#pragma once

#include <cstddef>
#include <optional>

namespace paimon::twitch {

// replaceScene: true between requests (swaps the scene so back returns to
// the list), false on entry from the queue.
void openRequestedLevel(int levelID, bool replaceScene);

// marks the request reviewed with the player's current progress, then opens it.
void playRequestAt(size_t index, bool replaceScene);

// queue index of that ID, if still there.
std::optional<size_t> indexOfRequest(int levelID);

} // namespace paimon::twitch
