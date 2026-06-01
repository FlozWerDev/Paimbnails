#include "LevelSearchHelpers.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>

using namespace geode::prelude;

namespace paimon::levelsearch {

void releaseSearchInputFocus(LevelSearchLayer* layer) {
    if (!layer || !layer->m_searchInput) return;

    auto* input = layer->m_searchInput;

    // 1. Desconectar el CCTextFieldTTF del IME dispatcher: esto es lo que
    //    realmente detiene el ruteo de teclas al text field.
    if (input->m_textField) {
        input->m_textField->detachWithIME();
    }

    // 2. Forzar el bookkeeping de "selected" a false para que el siguiente
    //    visit() no se re-attache al IME por su cuenta.
    input->m_selected = false;

    // 3. Avisar al wrapper que se deseleccione (limpia el cursor visual).
    //    Lo hacemos despues de limpiar m_selected.
    input->onClickTrackNode(false);
}

} // namespace paimon::levelsearch
