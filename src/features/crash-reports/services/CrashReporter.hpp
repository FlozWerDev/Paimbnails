#pragma once

namespace paimon::crash {

// Uploads Geode's leftover crashlogs with their matching session logs.
void reportPendingCrashes();

} // namespace paimon::crash
