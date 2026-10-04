#pragma once

#include "../../utils/Localization.hpp"

namespace paimon::animate {

inline bool spanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

inline char const* tr(char const* english, char const* spanishText) {
    return spanish() ? spanishText : english;
}

} // namespace paimon::animate
