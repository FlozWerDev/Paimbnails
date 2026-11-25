#pragma once

// reloadall / android foreground context changes invalidate gl resources:
// release under the old context (deleteHandles=true) or reset orphaned ids
// lazily under a new EGL context (deleteHandles=false).
namespace paimon::glreload {

void onBeforeGameReload();
void onContextLost();

} // namespace paimon::glreload
