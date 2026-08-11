#pragma once

// Popup del boton "i" de una fila: el recado que la persona escribio junto al
// nivel cuando lo mando desde tu pagina web.

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::twitch {

struct LevelRequest;

class TwitchMessagePopup : public geode::Popup {
public:
    // `levelName` y `author` son lo que ya sabemos del nivel; vacios mientras
    // todavia se esta cargando.
    static TwitchMessagePopup* create(
        LevelRequest const& request, std::string levelName, std::string author);

protected:
    bool init(LevelRequest const& request, std::string levelName, std::string author);
};

} // namespace paimon::twitch
