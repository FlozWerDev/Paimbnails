#pragma once

// Seam between the slot UI and the LevelSelect hooks: after mutating the
// store, refreshOfficialList() reconciles the live pages with the page order
// (added slots get real pages, hidden officials lose theirs) and repaints.

class LevelSelectLayer;

namespace paimon::officialslots {

void refreshOfficialList();
void syncPages(LevelSelectLayer* select);

} // namespace paimon::officialslots
