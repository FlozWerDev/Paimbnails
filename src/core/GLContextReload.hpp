#pragma once

// reloadAll() mata el contexto GL (los names creados por el mod quedan muertos):
// liberar en onBeforeGameReload() con el contexto viejo activo y recrear lazy.
namespace paimon::glreload {

void onBeforeGameReload();

} // namespace paimon::glreload
