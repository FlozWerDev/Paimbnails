#pragma once

// Al cambiar resolución/fullscreen/calidad de texturas, GD ejecuta
// GameManager::reloadAll(): recrea la ventana (y con ella el contexto GL) y
// purga CCTextureCache. Toda textura o shader creado por el mod que no venga
// de un archivo del search path queda con un GL name muerto — los sprites que
// la usen muestran basura o pedazos de otros spritesheets.
//
// onBeforeGameReload() se dispara ANTES de que RobTop toque nada, con el
// contexto viejo todavía activo: liberar texturas aquí hace glDeleteTextures
// sobre names que aún nos pertenecen. Cada servicio suelta sus texturas RAM
// y las recrea lazy después del reload (los datos fuente viven en disco).
namespace paimon::glreload {

void onBeforeGameReload();

} // namespace paimon::glreload
