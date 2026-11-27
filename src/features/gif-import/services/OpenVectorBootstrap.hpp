#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace paimon::gifimport {

struct OpenVectorBootstrapProgress {
    std::string stage;   // "resolving" | "downloading" | "extracting" | "done" | "error"
    float percent = 0.f; // 0.0 .. 1.0
    std::string message;
};

using OpenVectorBootstrapProgressCb = std::function<void(OpenVectorBootstrapProgress const&)>;
using OpenVectorBootstrapCompleteCb = std::function<void(bool success, std::string const& pathOrError)>;

// Complementary OpenVector (https://github.com/Microck/openvector, MIT) CLI bootstrapper.
class OpenVectorBootstrap {
public:
    static OpenVectorBootstrap& get();

    static constexpr char const* kRepoUrl = "https://github.com/Microck/openvector";
    static constexpr char const* kVersion = "0.1.6";

    std::filesystem::path bundledPath() const;
    std::filesystem::path locateBinary() const;
    bool exists() const;
    bool isDownloading() const { return m_downloading.load(); }
    void uninstall();

    static std::string releaseUrl();
    static std::string archiveEntry();

    // Extract a single regular file entry from a gzip-compressed POSIX tar (.tgz) buffer.
    static bool extractEntryFromTgz(
        std::uint8_t const* tgzData,
        std::size_t tgzSize,
        std::string const& targetEntry,
        std::vector<std::uint8_t>& outBytes,
        std::string& errorOut
    );

    void ensureInstalled(
        OpenVectorBootstrapProgressCb onProgress,
        OpenVectorBootstrapCompleteCb onComplete
    );

private:
    OpenVectorBootstrap() = default;
    std::atomic<bool> m_downloading{false};
};

} // namespace paimon::gifimport
