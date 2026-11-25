#pragma once

#include <string>

namespace paimon::utf8 {

// truncate to at most `limit` bytes without splitting a UTF-8 sequence.
inline void clampToLimit(std::string& text, size_t limit) {
    if (text.size() <= limit) return;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    text.resize(cut);
}

} // namespace paimon::utf8
