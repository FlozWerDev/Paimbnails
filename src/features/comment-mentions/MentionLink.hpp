#pragma once

#include <string>

namespace paimon::mentions {

// Resolves a username to its account ID via the GD servers and opens that
// user's ProfilePage. Shows a loading/error notification. Safe to call from
// the main thread (the actual request is async).
void openProfile(std::string const& username);

} // namespace paimon::mentions
