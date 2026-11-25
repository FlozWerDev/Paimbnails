#include "../src/utils/FilesystemPath.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    auto const text = std::u8string(u8"carpeta con espacios/miniatura-ñ-雪.png");
    std::filesystem::path const path(text);
    assert(paimon::paths::toUtf8(path) == std::string(text.begin(), text.end()));
    assert(paimon::paths::toStdPath(path) == path);
    assert(paimon::paths::toUtf8(std::filesystem::path{}).empty());
    auto copied = paimon::paths::toStdPath(std::filesystem::path(text));
    assert(copied == path); // conversion owns its result even for temporary inputs
}
