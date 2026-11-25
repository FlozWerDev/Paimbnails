// PackCache disk roundtrip: corrupt entries read as misses (file purged),
// valid entries read as hits. Covers the crash #6 hardening in
// src/features/texture-studio/packgen/PackCache.hpp (pre-size bound,
// magic/version/size/checksum checks, tmp+rename publication).

#include "../src/features/texture-studio/packgen/PackCache.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// Poison files dropped before the entry stream is opened are unlinked on all
// platforms. Drops issued while the std::ifstream is still in scope cannot
// unlink on Windows (MSVC opens without delete-sharing and production ignores
// the error); the entry still reads as a miss there. Deletion after an open
// stream is therefore only enforced on POSIX builds.
#ifdef _WIN32
#define ASSERT_PURGED_AFTER_OPEN(path) ((void)0)
#else
#define ASSERT_PURGED_AFTER_OPEN(path) assert(!fs::exists(path))
#endif
using paimon::texture_studio::packgen::PackCache;
using paimon::texture_studio::packgen::NodeKey;
using paimon::texture_studio::packgen::hashBytes;
using paimon::texture_studio::packgen::kPackCacheDiskHeaderSize;
using paimon::texture_studio::packgen::kPackCacheDiskMagic;

namespace {

fs::path freshDir() {
    auto dir = fs::temp_directory_path() /
        ("paimbnails-packcache-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

fs::path diskPathFor(fs::path const& dir, NodeKey const& key) {
    char name[48];
    std::snprintf(name, sizeof(name), "%016llx_v%d.bin",
                  static_cast<unsigned long long>(key.hash), key.version);
    return dir / name;
}

// little-endian wire header: {magic u32, version u32, payloadLen u64, fnv1a64 u64}.
std::vector<std::uint8_t> headerBytes(std::uint32_t magic, std::uint32_t version,
                                      std::uint64_t payloadLen, std::uint64_t checksum) {
    std::vector<std::uint8_t> header(kPackCacheDiskHeaderSize, 0);
    std::memcpy(header.data(), &magic, 4);
    std::memcpy(header.data() + 4, &version, 4);
    std::memcpy(header.data() + 8, &payloadLen, 8);
    std::memcpy(header.data() + 16, &checksum, 8);
    return header;
}

void writeFile(fs::path const& path, std::vector<std::uint8_t> const& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    if (!bytes.empty()) {
        out.write(reinterpret_cast<char const*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    out.close();
    assert(out);
}

PackCache::Bytes blob(std::size_t size, std::uint8_t fill) {
    return PackCache::Bytes(size, fill);
}

} // namespace

int main() {
    auto dir = freshDir();

    // 1. memory + disk roundtrip: store, then a fresh cache hits via disk.
    {
        PackCache cache;
        cache.setDiskDir(dir);
        NodeKey key{0x1234abcdULL, 7};
        auto data = blob(256, 0x5a);
        cache.store(key, data);
        auto const* hit = cache.lookup(key);
        assert(hit && *hit == data);
        assert(fs::exists(diskPathFor(dir, key)));

        PackCache cold;
        cold.setDiskDir(dir);
        auto const* diskHit = cold.lookup(key);
        assert(diskHit && *diskHit == data);
        assert(cold.stats().diskHits == 1);
    }

    // 2a. truncated below the header size: dropped by the pre-size bound
    // before any stream opens, so the purge works on every platform.
    {
        NodeKey key{0xAAAAULL, 1};
        writeFile(diskPathFor(dir, key), std::vector<std::uint8_t>(10, 0x11));
        PackCache cache;
        cache.setDiskDir(dir);
        assert(cache.lookup(key) == nullptr);
        assert(!fs::exists(diskPathFor(dir, key))); // poison purged
        assert(cache.stats().misses == 1);
    }

    // 2b. truncated body: header claims 64 payload bytes, only 10 on disk.
    {
        NodeKey key{0xAAABULL, 1};
        auto header = headerBytes(kPackCacheDiskMagic, 1, 64, 0);
        header.insert(header.end(), 10, 0x11);
        auto const path = diskPathFor(dir, key);
        writeFile(path, header);
        PackCache cache;
        cache.setDiskDir(dir);
        assert(cache.lookup(key) == nullptr);
        ASSERT_PURGED_AFTER_OPEN(path);
    }

    // 3. corrupt magic with an otherwise well-formed entry.
    {
        NodeKey key{0xBBBBULL, 1};
        auto payload = blob(32, 0x33);
        auto header = headerBytes(0xDEADBEEF, 1, payload.size(),
                                  hashBytes(payload.data(), payload.size()));
        header.insert(header.end(), payload.begin(), payload.end());
        auto const path = diskPathFor(dir, key);
        writeFile(path, header);
        PackCache cache;
        cache.setDiskDir(dir);
        assert(cache.lookup(key) == nullptr);
        ASSERT_PURGED_AFTER_OPEN(path);
    }

    // 4. oversize entry: file bigger than header + maxDiskEntryBytes.
    {
        NodeKey key{0xCCCCULL, 1};
        auto payload = blob(128, 0x44);
        auto header = headerBytes(kPackCacheDiskMagic, 1, payload.size(),
                                  hashBytes(payload.data(), payload.size()));
        header.insert(header.end(), payload.begin(), payload.end());
        auto const path = diskPathFor(dir, key);
        writeFile(path, header);
        PackCache cache;
        cache.setDiskDir(dir);
        cache.setMaxDiskEntryBytes(64);
        assert(cache.lookup(key) == nullptr);
        assert(!fs::exists(path));
    }

    // 5. store() never publishes blobs over the cap to disk.
    {
        NodeKey key{0xDDDDULL, 1};
        PackCache cache;
        cache.setDiskDir(dir);
        cache.setMaxDiskEntryBytes(64);
        cache.store(key, blob(128, 0x55));
        assert(!fs::exists(diskPathFor(dir, key)));
        // memory path still works.
        assert(cache.lookup(key) != nullptr);
    }

    // 6. version mismatch: v1 header under a v2 file name.
    {
        NodeKey key{0xEEEEULL, 2};
        auto payload = blob(16, 0x66);
        auto header = headerBytes(kPackCacheDiskMagic, 1, payload.size(),
                                  hashBytes(payload.data(), payload.size()));
        header.insert(header.end(), payload.begin(), payload.end());
        auto const path = diskPathFor(dir, key);
        writeFile(path, header);
        PackCache cache;
        cache.setDiskDir(dir);
        assert(cache.lookup(key) == nullptr);
        ASSERT_PURGED_AFTER_OPEN(path);
    }

    // 7. checksum mismatch: payload flipped after publication.
    {
        PackCache cache;
        cache.setDiskDir(dir);
        NodeKey key{0xFFFFULL, 3};
        cache.store(key, blob(48, 0x77));
        auto const path = diskPathFor(dir, key);
        assert(fs::exists(path));
        {
            std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
            assert(io);
            io.seekp(static_cast<std::streamoff>(kPackCacheDiskHeaderSize));
            char flip = static_cast<char>(0x78);
            io.write(&flip, 1);
            io.close();
        }
        PackCache cold;
        cold.setDiskDir(dir);
        assert(cold.lookup(key) == nullptr);
        ASSERT_PURGED_AFTER_OPEN(path);
    }

    // 8. regression guard: no stray .tmp files left behind by publication.
    for (auto const& entry : fs::directory_iterator(dir)) {
        assert(entry.path().extension() != ".tmp");
    }

    fs::remove_all(dir);
}
