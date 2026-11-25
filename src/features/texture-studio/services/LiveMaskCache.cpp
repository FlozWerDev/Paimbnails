#include "LiveMaskCache.hpp"

#include "../../../utils/AtomicFileWrite.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace paimon::texture_studio::live_mask_cache {

namespace {

// bump when the analyzer, catalog or texel layout changes meaning.
constexpr std::uint32_t kFormatVersion = 2;
constexpr char kEngineRevision[] = "live-mask-r2";
constexpr char kExtension[] = ".lmc";
constexpr std::size_t kHeaderBytes = 28;
// masks never exceed the runtime's 4096 texture limit.
constexpr std::uint32_t kMaxSide = 4096;
constexpr std::size_t kMaxFiles = 160;
constexpr std::uintmax_t kMaxBytes = 160ull * 1024 * 1024;

std::uint64_t fnv1a(std::string const& text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool describeFile(std::string const& path, std::string& out) {
    if (path.empty()) {
        out += "|-";
        return true;
    }
    std::error_code ec;
    std::filesystem::path file(path);
    auto size = std::filesystem::file_size(file, ec);
    if (ec) return false;
    auto stamp = std::filesystem::last_write_time(file, ec);
    if (ec) return false;
    out += '|';
    out += path;
    out += '|';
    out += std::to_string(size);
    out += '|';
    out += std::to_string(stamp.time_since_epoch().count());
    return true;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::uint32_t get32(std::uint8_t const* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

template <class Visit>
void forEachEntry(std::filesystem::path const& dir, Visit&& visit) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (it->path().extension() != kExtension || !it->is_regular_file(entryError)) continue;
        visit(*it);
    }
}

void prune(std::filesystem::path const& dir) {
    struct Entry {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type stamp;
    };
    std::vector<Entry> entries;
    forEachEntry(dir, [&](std::filesystem::directory_entry const& item) {
        std::error_code ec;
        auto size = item.file_size(ec);
        if (ec) return;
        auto stamp = item.last_write_time(ec);
        if (ec) return;
        entries.push_back({item.path(), size, stamp});
    });
    std::sort(entries.begin(), entries.end(),
        [](Entry const& a, Entry const& b) { return a.stamp > b.stamp; });
    std::uintmax_t total = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        total += entries[i].size;
        std::error_code ec;
        if (i >= kMaxFiles || total > kMaxBytes) std::filesystem::remove(entries[i].path, ec);
    }
}

bool decode(std::vector<std::uint8_t> const& bytes, std::uint64_t content,
            std::vector<std::uint16_t>& texels, int& width, int& height, bool& corrupt) {
    corrupt = true;
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), "PLMC", 4) != 0) return false;
    if (get32(bytes.data() + 4) != kFormatVersion) return false;
    std::uint64_t stored = static_cast<std::uint64_t>(get32(bytes.data() + 8)) |
        (static_cast<std::uint64_t>(get32(bytes.data() + 12)) << 32);
    std::uint32_t w = get32(bytes.data() + 16);
    std::uint32_t h = get32(bytes.data() + 20);
    std::uint32_t runs = get32(bytes.data() + 24);
    if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide) return false;
    if (bytes.size() != kHeaderBytes + static_cast<std::size_t>(runs) * 4) return false;
    std::size_t total = static_cast<std::size_t>(w) * h;
    auto const* runData = bytes.data() + kHeaderBytes;
    std::size_t covered = 0;
    for (std::uint32_t i = 0; i < runs; ++i) {
        auto const* p = runData + static_cast<std::size_t>(i) * 4;
        covered += static_cast<std::size_t>(p[2] | (p[3] << 8)) + 1;
    }
    if (covered != total) return false;
    corrupt = false;
    // a valid file for other options or source files: a plain miss.
    if (stored != content) return false;
    texels.clear();
    texels.reserve(total);
    for (std::uint32_t i = 0; i < runs; ++i) {
        auto const* p = runData + static_cast<std::size_t>(i) * 4;
        auto value = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        texels.insert(texels.end(), static_cast<std::size_t>(p[2] | (p[3] << 8)) + 1, value);
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

}  // namespace

Key makeKey(std::string const& png, std::string const& plist,
            LiveMaskOptions const& options, bool forceTint, int scope) {
    if (png.empty()) return {};
    std::string identity = std::string(kEngineRevision) + '|' + png + '|' + plist +
        (forceTint ? "|force" : "|catalog") + '|' + std::to_string(scope);
    std::string content = identity;
    if (!describeFile(png, content) || !describeFile(plist, content)) return {};
    content += '|' + std::to_string(static_cast<int>(std::lround(options.softness * 1000.f)));
    content += '|' + std::to_string(options.clusterPrecision);
    content += '|' + std::to_string(options.edgeCleanup);
    content += '|' + std::to_string(options.alphaCutoff);
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(fnv1a(identity)));
    return {hex, fnv1a(content)};
}

bool load(std::filesystem::path const& dir, Key const& key,
          std::vector<std::uint16_t>& texels, int& width, int& height) {
    if (dir.empty() || !key.valid()) return false;
    auto path = dir / (key.file + kExtension);
    try {
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) return false;
            auto size = static_cast<std::streamoff>(in.tellg());
            if (size < static_cast<std::streamoff>(kHeaderBytes) ||
                size > static_cast<std::streamoff>(kHeaderBytes + 4ull * kMaxSide * kMaxSide)) {
                in.close();
                std::error_code ec;
                std::filesystem::remove(path, ec);
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            in.seekg(0);
            if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return false;
        }
        bool corrupt = false;
        if (!decode(bytes, key.content, texels, width, height, corrupt)) {
            if (corrupt) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
            texels.clear();
            return false;
        }
        // touch the entry so pruning keeps recently used masks.
        std::error_code ec;
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
        return true;
    } catch (...) {
        texels.clear();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return false;
    }
}

void store(std::filesystem::path const& dir, Key const& key,
           std::vector<std::uint16_t> const& texels, int width, int height) {
    if (dir.empty() || !key.valid() || width <= 0 || height <= 0) return;
    if (width > static_cast<int>(kMaxSide) || height > static_cast<int>(kMaxSide)) return;
    if (texels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return;
    try {
        // run-length texels: transparent gaps and flat fills collapse to a few runs.
        std::vector<std::uint8_t> bytes;
        bytes.reserve(kHeaderBytes + texels.size() / 8);
        bytes.insert(bytes.end(), {'P', 'L', 'M', 'C'});
        put32(bytes, kFormatVersion);
        put32(bytes, static_cast<std::uint32_t>(key.content));
        put32(bytes, static_cast<std::uint32_t>(key.content >> 32));
        put32(bytes, static_cast<std::uint32_t>(width));
        put32(bytes, static_cast<std::uint32_t>(height));
        put32(bytes, 0);
        std::uint32_t runs = 0;
        for (std::size_t i = 0; i < texels.size();) {
            std::uint16_t value = texels[i];
            std::size_t count = 1;
            while (i + count < texels.size() && texels[i + count] == value && count < 65536) ++count;
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes.push_back(static_cast<std::uint8_t>((count - 1) & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>((count - 1) >> 8));
            ++runs;
            i += count;
        }
        for (int b = 0; b < 4; ++b) bytes[24 + b] = static_cast<std::uint8_t>(runs >> (8 * b));

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!paimon::file::writeAtomically(dir / (key.file + kExtension),
                std::span<std::uint8_t const>(bytes.data(), bytes.size()))) return;
        prune(dir);
    } catch (...) {
        // the mask in memory is still valid; only the cache entry is lost.
    }
}

void clear(std::filesystem::path const& dir) {
    if (dir.empty()) return;
    std::vector<std::filesystem::path> files;
    forEachEntry(dir, [&](std::filesystem::directory_entry const& item) { files.push_back(item.path()); });
    for (auto const& file : files) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
}

std::uintmax_t sizeOnDisk(std::filesystem::path const& dir) {
    std::uintmax_t total = 0;
    if (dir.empty()) return total;
    forEachEntry(dir, [&](std::filesystem::directory_entry const& item) {
        std::error_code ec;
        auto size = item.file_size(ec);
        if (!ec) total += size;
    });
    return total;
}

}  // namespace paimon::texture_studio::live_mask_cache
