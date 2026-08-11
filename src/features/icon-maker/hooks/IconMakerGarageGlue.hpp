#pragma once

class GJGarageLayer;

namespace paimon::icon_maker::garage {

// El Creador de Iconos ya no pone boton propio en el garage: se entra desde la
// banda inferior del popup de Paimon Icons.

// Re-applies exact colors on the garage's main preview after GD re-tints it.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icon_maker::garage
