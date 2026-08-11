#pragma once

// Abre el nivel pedido directamente en su LevelInfoLayer, bajando la info del
// servidor si todavia no la tenemos. Lo usan tanto la cola como las flechas de
// navegacion del propio LevelInfoLayer.

#include <cstddef>
#include <optional>

namespace paimon::twitch {

// replaceScene: true al navegar entre pedidos (intercambia la escena para que
// el boton de volver siga llevando a la lista), false al entrar desde la cola.
void openRequestedLevel(int levelID, bool replaceScene);

// Marca el pedido como revisado con el avance actual del jugador y lo abre.
void playRequestAt(size_t index, bool replaceScene);

// Indice del pedido con esa ID dentro de la cola, si sigue ahi.
std::optional<size_t> indexOfRequest(int levelID);

} // namespace paimon::twitch
