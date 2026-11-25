#pragma once

#include "../engine/LiveMaskAnalyzer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace paimon::texture_studio::live_mask_cache {

// one cache file per source texture. `content` covers the source files
// (path, size, mtime), the analysis options and the engine revision; a file
// holding another content hash is a miss and gets overwritten. Editing
// sliders therefore replaces one entry per texture instead of piling up.
struct Key {
    std::string file;
    std::uint64_t content = 0;
    bool valid() const { return !file.empty(); }
};

Key makeKey(std::string const& png, std::string const& plist,
            LiveMaskOptions const& options, bool forceTint, int scope);

// worker-thread safe: plain file I/O, no Cocos/Geode state. Never throw:
// any I/O or format error is a miss (and a corrupt file is deleted).
bool load(std::filesystem::path const& dir, Key const& key,
          std::vector<std::uint16_t>& texels, int& width, int& height);
void store(std::filesystem::path const& dir, Key const& key,
           std::vector<std::uint16_t> const& texels, int width, int height);

void clear(std::filesystem::path const& dir);
std::uintmax_t sizeOnDisk(std::filesystem::path const& dir);

}  // namespace paimon::texture_studio::live_mask_cache
