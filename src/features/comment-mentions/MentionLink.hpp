#pragma once

#include <string>

namespace paimon::mentions {

// async request; safe to call from the main thread.
void openProfile(std::string const& username);

} // namespace paimon::mentions
