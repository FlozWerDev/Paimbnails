#include "TranslatorLanguages.hpp"

namespace paimon::translate {

std::vector<LanguageEntry> const& languages() {
    static std::vector<LanguageEntry> const entries = {
        {"en", "English", "English"},
        {"es", "Español", "Spanish"},
        {"fr", "Français", "French"},
        {"de", "Deutsch", "German"},
        {"it", "Italiano", "Italian"},
        {"pt", "Português", "Portuguese"},
        {"nl", "Nederlands", "Dutch"},
        {"ru", "Русский", "Russian"},
        {"hu", "Magyar", "Hungarian"},
    };
    return entries;
}

LanguageEntry const* findLanguage(std::string_view code) {
    for (auto const& entry : languages()) {
        if (code == entry.code) return &entry;
    }
    return nullptr;
}

}
