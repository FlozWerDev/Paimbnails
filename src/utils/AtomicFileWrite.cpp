#include "AtomicFileWrite.hpp"

#include <Geode/platform/platform.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <utility>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace paimon::file {
namespace {
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};
}

bool writeAtomically(std::filesystem::path const& path, std::span<uint8_t const> bytes) {
    if (bytes.size() > static_cast<uintmax_t>((std::numeric_limits<std::streamsize>::max)())) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    static std::atomic<uint64_t> sequence{0};
#ifdef GEODE_IS_WINDOWS
    auto process = GetCurrentProcessId();
#else
    auto process = getpid();
#endif
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tempPath = path;
    tempPath += "." + std::to_string(process) + "." + std::to_string(stamp) + "." +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
    TemporaryFile temp{std::move(tempPath)};
    {
        std::ofstream out(temp.path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!bytes.empty()) out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) return false;
    }
    // windows needs an explicit replace flag; rename replaces on posix.
#ifdef GEODE_IS_WINDOWS
    return MoveFileExW(temp.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::filesystem::rename(temp.path, path, ec);
    return !ec;
#endif
}

bool writeAtomically(std::filesystem::path const& path, std::string_view text) {
    return writeAtomically(path, std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(text.data()), text.size()));
}
}
