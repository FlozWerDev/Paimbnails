#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace geode::utils::string {

inline std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace geode::utils::string
