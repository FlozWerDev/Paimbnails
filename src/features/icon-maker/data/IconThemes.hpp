#pragma once

#include "FillSpec.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// El blanco ("extra") son los ojos y los brillos de los iconos del juego, asi
// que solo se pinta cuando el tema lo pide expresamente.
struct IconTheme {
    std::string name;
    FillSpec main;
    FillSpec secondary;
    FillSpec tertiary;
    FillSpec glow;
    bool paintExtra = false;
    FillSpec extra;
};

std::vector<IconTheme> const& iconThemes();

// Tema armado con los colores de jugador de ahora mismo. Devuelve false si
// GameManager todavia no esta en pie.
bool currentKitTheme(IconTheme& out);

// Relleno que le toca a una zona; false cuando el tema no la pinta.
bool themeFillFor(IconTheme const& theme, std::string_view slotKey, FillSpec& out);

}  // namespace paimon::icon_maker
