#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace paimon::file {

inline std::vector<std::uint8_t> readBytes(
    std::filesystem::path const& path,
    std::uintmax_t limit = std::numeric_limits<std::size_t>::max()
) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > limit || size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) return {};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    // read the stat size so a file growing during import cannot expand the allocation.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) return {};
    return bytes;
}

} // namespace paimon::file
