#pragma once

class LevelSelectLayer;

namespace paimon::officialslots {

void refreshOfficialList();
void syncPages(LevelSelectLayer* select);
void selectVisiblePage(LevelSelectLayer* select, int index);

} // namespace paimon::officialslots
