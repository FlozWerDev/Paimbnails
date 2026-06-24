#include "LevelSearchHelpers.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::levelsearch {

void releaseSearchInputFocus(LevelSearchLayer* layer) {
    if (!layer || !layer->m_searchInput) return;

    auto* input = layer->m_searchInput;

    // 1. Detach CCTextFieldTTF from the IME dispatcher; this is what actually stops key routing.
    if (input->m_textField) {
        input->m_textField->detachWithIME();
    }

    // 2. Force m_selected=false so the next visit() doesn't re-attach to the IME.
    input->m_selected = false;

    // 3. Tell the wrapper to deselect (clears the visual cursor), after clearing m_selected.
    input->onClickTrackNode(false);
}

} // namespace paimon::levelsearch
