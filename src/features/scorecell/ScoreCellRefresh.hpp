#pragma once

namespace paimon::scorecell {

// Re-applies the GJScoreCell FX (icon gradient + hover) to every score cell
// currently in the running scene.  Called when the settings popup closes so
// changes are visible without scrolling/reopening the leaderboard.
void refreshAllCells();

} // namespace paimon::scorecell
