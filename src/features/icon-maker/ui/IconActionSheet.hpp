#pragma once
// Lista de acciones secundarias. Existe para que la columna de la derecha
// muestre solo lo que se usa siempre: todo lo demas (duplicar, renombrar,
// borrar, copiar estilo...) vive aqui detras de un boton "...".

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::icon_maker {

class IconActionSheet : public geode::Popup {
public:
    struct Action {
        std::string label;
        std::string desc;
        std::function<void()> run;
        bool destructive = false;
    };

    // The sheet closes before running the action, so callbacks are free to
    // open another popup or rebuild the scene behind it.
    static IconActionSheet* create(std::string title, std::vector<Action> actions);

protected:
    bool init(std::string title, std::vector<Action> actions);
};

}  // namespace paimon::icon_maker
