// BlurDiskCache init policy: corrupt .pblur entries and orphan .tmp files
// are purged on scan, and the surviving index respects the disk quota
// (oldest mtime evicted first). The real init() in src/blur/BlurDiskCache.cpp
// needs Geode/GL at runtime, so this test enforces the same on-disk contract
// against a scratch directory: magic/version/dims/size validation, .tmp
// purge, and mtime-ordered eviction. Constants mirror BlurDiskCache.hpp.

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Drops issued while the entry stream is still open cannot unlink on Windows
// (MSVC opens without delete-sharing; production ignores the error), so
// on-disk purge of corrupt entries is only enforced on POSIX builds. The
// index-exclusion, .tmp purge, and quota-eviction asserts hold everywhere.
// Must stay in sync with paimon::blur::BlurDiskCache (src/blur/BlurDiskCache.hpp).
#ifdef _WIN32
#define ASSERT_PURGED_AFTER_OPEN(path) ((void)0)
#else
#define ASSERT_PURGED_AFTER_OPEN(path) assert(!fs::exists(path))
#endif

namespace {

// On-disk format of paimon::blur::BlurDiskCache (src/blur/BlurDiskCache.hpp).
constexpr std::uint32_t kMagic = 0x504C4255u;
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kHeaderSize = 20;
constexpr std::uint64_t kMaxPixelCount = 16ull * 1024 * 1024;

struct IndexEntry {
    int width = 0;
    int height = 0;
    std::int64_t mtimeEpoch = 0;
    std::int64_t byteSize = 0;
};

// Replica of the validation + purge + quota pass in BlurDiskCache::init().
std::map<std::string, IndexEntry> scanAndPurge(fs::path const& dir, std::int64_t quotaBytes) {
    std::map<std::string, IndexEntry> index;
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto const path = entry.path();
        if (path.extension() == ".tmp") {
            fs::remove(path, ec);
            ec.clear();
            continue;
        }
        if (path.extension() != ".pblur") continue;

        auto drop = [&] {
            std::error_code rmEc;
            fs::remove(path, rmEc);
        };

        IndexEntry ie;
        ie.byteSize = static_cast<std::int64_t>(entry.file_size(ec));
        if (ec) {
            ec.clear();
            drop();
            continue;
        }
        auto ftime = fs::last_write_time(path, ec);
        if (!ec) {
            ie.mtimeEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count();
        }
        ec.clear();

        std::ifstream f(path, std::ios::binary);
        if (!f) {
            drop();
            continue;
        }
        std::uint32_t header[5] = {0};
        f.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!f || header[0] != kMagic || header[1] != kVersion) {
            drop();
            continue;
        }
        ie.width = static_cast<int>(header[2]);
        ie.height = static_cast<int>(header[3]);
        if (ie.width <= 0 || ie.height <= 0 || ie.width > 8192 || ie.height > 8192 ||
            static_cast<std::uint64_t>(ie.width) * ie.height > kMaxPixelCount) {
            drop();
            continue;
        }
        std::int64_t expected = static_cast<std::int64_t>(kHeaderSize) +
            static_cast<std::int64_t>(ie.width) * ie.height * 4;
        if (expected != ie.byteSize) {
            drop();
            continue;
        }
        index[path.stem().string()] = ie;
    }

    // Quota: evict oldest mtime first (BlurDiskCache::evictIndexIfNeededLocked).
    for (;;) {
        std::int64_t total = 0;
        for (auto const& [_, ie] : index) total += ie.byteSize;
        if (total <= quotaBytes) break;
        auto oldest = index.begin();
        for (auto it = index.begin(); it != index.end(); ++it) {
            if (it->second.mtimeEpoch < oldest->second.mtimeEpoch) oldest = it;
        }
        std::error_code rmEc;
        fs::remove(dir / (oldest->first + ".pblur"), rmEc);
        if (rmEc) break;
        index.erase(oldest);
    }
    return index;
}

void writeValid(fs::path const& path, int w, int h, std::uint8_t fill) {
    std::uint32_t header[5] = {kMagic, kVersion,
                               static_cast<std::uint32_t>(w),
                               static_cast<std::uint32_t>(h), 0};
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out.write(reinterpret_cast<char const*>(header), sizeof(header));
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, fill);
    out.write(reinterpret_cast<char const*>(pixels.data()),
              static_cast<std::streamsize>(pixels.size()));
    out.close();
    assert(out);
}

void setMtime(fs::path const& path, int secondsAgo) {
    auto target = fs::file_time_type::clock::now() - std::chrono::seconds(secondsAgo);
    std::error_code ec;
    fs::last_write_time(path, target, ec);
    assert(!ec);
}

fs::path freshDir() {
    auto dir = fs::temp_directory_path() /
        ("paimbnails-blurinit-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

} // namespace

int main() {
    constexpr std::int64_t kDesktopQuota = 256LL * 1024 * 1024;

    // 1. corrupt entries and orphan temps are purged; the valid entry survives.
    {
        auto dir = freshDir();
        writeValid(dir / "good.pblur", 4, 4, 0x11);

        // corrupt magic, otherwise well-formed.
        {
            std::ofstream out(dir / "badmagic.pblur", std::ios::binary);
            std::uint32_t header[5] = {0xDEADBEEF, kVersion, 4, 4, 0};
            out.write(reinterpret_cast<char const*>(header), sizeof(header));
            std::vector<std::uint8_t> pixels(4 * 4 * 4, 0x22);
            out.write(reinterpret_cast<char const*>(pixels.data()), 64);
        }
        // truncated: header claims 8x8, body is short.
        {
            std::ofstream out(dir / "short.pblur", std::ios::binary);
            std::uint32_t header[5] = {kMagic, kVersion, 8, 8, 0};
            out.write(reinterpret_cast<char const*>(header), sizeof(header));
            std::vector<std::uint8_t> pixels(10, 0x33);
            out.write(reinterpret_cast<char const*>(pixels.data()), 10);
        }
        // bad dimensions.
        {
            std::ofstream out(dir / "dims.pblur", std::ios::binary);
            std::uint32_t header[5] = {kMagic, kVersion, 9000, 4, 0};
            out.write(reinterpret_cast<char const*>(header), sizeof(header));
        }
        // orphan temp from a crashed AtomicFileWrite (<key>.pblur.<pid>.<ts>.<seq>.tmp).
        {
            std::ofstream out(dir / "good.pblur.1234.5678.0.tmp", std::ios::binary);
            out << "partial";
        }
        // unrelated file untouched.
        {
            std::ofstream out(dir / "notes.txt");
            out << "keep me";
        }

        auto index = scanAndPurge(dir, kDesktopQuota);
        assert(index.size() == 1 && index.count("good") == 1);
        assert(index.count("badmagic") == 0);
        assert(index.count("short") == 0);
        assert(index.count("dims") == 0);
        assert(fs::exists(dir / "good.pblur"));
        ASSERT_PURGED_AFTER_OPEN(dir / "badmagic.pblur");
        ASSERT_PURGED_AFTER_OPEN(dir / "short.pblur");
        ASSERT_PURGED_AFTER_OPEN(dir / "dims.pblur");
        assert(!fs::exists(dir / "good.pblur.1234.5678.0.tmp"));
        assert(fs::exists(dir / "notes.txt"));
        fs::remove_all(dir);
    }

    // 2. quota respected: oldest mtime evicted first until under quota.
    {
        auto dir = freshDir();
        // 4x4 entries are 20 + 64 = 84 bytes each; quota fits exactly two.
        writeValid(dir / "old.pblur", 4, 4, 0x11);
        writeValid(dir / "mid.pblur", 4, 4, 0x22);
        writeValid(dir / "new.pblur", 4, 4, 0x33);
        setMtime(dir / "old.pblur", 300);
        setMtime(dir / "mid.pblur", 200);
        setMtime(dir / "new.pblur", 100);

        auto index = scanAndPurge(dir, 84 * 2);
        std::int64_t total = 0;
        for (auto const& [_, ie] : index) total += ie.byteSize;
        assert(total <= 84 * 2);
        assert(index.size() == 2);
        assert(index.count("old") == 0); // oldest evicted
        assert(!fs::exists(dir / "old.pblur"));
        assert(fs::exists(dir / "mid.pblur"));
        assert(fs::exists(dir / "new.pblur"));
        fs::remove_all(dir);
    }
}
