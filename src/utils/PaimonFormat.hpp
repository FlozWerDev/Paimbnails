#pragma once

#include <vector>
#include <cstdint>
#include <filesystem>

namespace PaimonFormat {
    bool save(std::filesystem::path const& path, std::vector<uint8_t> const& data);
    std::vector<uint8_t> load(std::filesystem::path const& path);
}
