#pragma once

#include <filesystem>
#include <string>

namespace geode::utils::string {
// Only serialize paths for standalone I/O tests. File opening uses production code.
inline std::string pathToString(std::filesystem::path const& path) {
    auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}
}
