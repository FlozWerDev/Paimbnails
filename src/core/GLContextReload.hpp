#pragma once

// reloadall kills the gl context: release under the old context, recreate lazily.
namespace paimon::glreload {

void onBeforeGameReload();

} // namespace paimon::glreload
