#include "../src/features/translator/TranslatorText.hpp"

#include <cassert>
#include <string>

using namespace paimon::translate;

namespace {

std::string privateUseGlyph() {
    // U+E000, the range GD and texture packs use for custom glyphs.
    return std::string("\xEE\x80\x80");
}

std::string emojiGlyph() {
    // U+1F600: a real emoji that the API can translate around.
    return std::string("\xF0\x9F\x98\x80");
}

}

int main() {
    assert(stripColorTags("hola <cg>mundo</c>!") == "hola mundo!");
    assert(stripColorTags("<c>rojo</c>") == "rojo");
    assert(stripColorTags("sin etiquetas") == "sin etiquetas");
    // An unterminated tag is text, not a color code.
    assert(stripColorTags("a <cg") == "a <cg");
    // '<' followed by something that is not a GD color code survives.
    assert(stripColorTags("2 < 3") == "2 < 3");

    assert(sanitizeForTranslation("<cr>hola</c> " + privateUseGlyph()) == "hola ");
    assert(sanitizeForTranslation("linea 1\nlinea 2") == "linea 1\nlinea 2");
    assert(sanitizeForTranslation("tab\tok") == "tab\tok");
    assert(sanitizeForTranslation(privateUseGlyph() + "visible") == "visible");
    assert(sanitizeForTranslation(emojiGlyph() + " hola") == emojiGlyph() + " hola");
    // NUL and other control bytes never reach the translation service.
    assert(sanitizeForTranslation(std::string("a\0b", 3)) == "ab");

    assert(isTranslatable("hello"));
    assert(isTranslatable("hola mundo"));
    assert(isTranslatable("Привет"));
    assert(!isTranslatable(""));
    assert(!isTranslatable("   "));
    assert(!isTranslatable("1234"));
    assert(!isTranslatable("!!!"));

    assert(clampUtf8("abcdef", 4) == "abcd");
    assert(clampUtf8("abc", 10) == "abc");
    // "añ" is 3 bytes; cutting at 2 must not split the two-byte ñ.
    assert(clampUtf8("a\xC3\xB1", 2) == "a");
    assert(clampUtf8("a\xC3\xB1", 3) == "a\xC3\xB1");
    assert(clampUtf8("", 4).empty());
}
