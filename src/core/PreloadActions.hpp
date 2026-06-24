#pragma once

// Shared preload implementation for the 22 main-level thumbnails + emote catalog.
// Call once per session from the caller that wins tryClaimPreload().
namespace paimon::preload {

void startFullPreload();

} // namespace paimon::preload
