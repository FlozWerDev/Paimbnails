#pragma once

namespace paimon::crash {

// Uploads the crashlogs Geode left behind on previous launches, together with
// the session log that goes with each one.
void reportPendingCrashes();

} // namespace paimon::crash
