#pragma once

// Seam between the slot UI and the LevelSelect hooks: after mutating the
// store, refreshOfficialList() repaints each live LevelPage via updateDynamicPage.

namespace paimon::officialslots {

void refreshOfficialList();

} // namespace paimon::officialslots
