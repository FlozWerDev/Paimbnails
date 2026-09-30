#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace paimon::file {
bool writeAtomically(std::filesystem::path const& path, std::span<uint8_t const> bytes);
bool writeAtomically(std::filesystem::path const& path, std::string_view text);
}
