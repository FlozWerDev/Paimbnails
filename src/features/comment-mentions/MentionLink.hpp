#pragma once

#include <string>

namespace paimon::mentions {

// Async request; safe to call from the main thread.
void openProfile(std::string const& username);

} // namespace paimon::mentions
