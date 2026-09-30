#include "LevelSearchHelpers.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::levelsearch {

void releaseSearchInputFocus(LevelSearchLayer* layer) {
    if (!layer || !layer->m_searchInput) return;

    auto* input = layer->m_searchInput;

    // order matters: detach ime first so keys stop routing, then clear flags.
    if (input->m_textField) {
        input->m_textField->detachWithIME();
    }

    input->m_selected = false;
    input->onClickTrackNode(false);
}

} // namespace paimon::levelsearch
