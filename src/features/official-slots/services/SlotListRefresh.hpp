#pragma once

// Seam between the slot UI and the LevelSelect hooks.
//
// The editor and the manager run before (and without) the hooks: they mutate
// the store and then call refreshOfficialList() so the official list redraws
// itself. The hooks translation unit implements it by rebuilding the scroll
// pages; until then this is a link-time contract only. Calling it with the
// hooks disabled is a no-op, never a crash: the list simply shows the new
// state the next time it is built.

namespace paimon::officialslots {

void refreshOfficialList();

} // namespace paimon::officialslots
