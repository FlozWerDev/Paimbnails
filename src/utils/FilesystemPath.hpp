#pragma once

#include <filesystem>
#include <string>

#if __has_include(<asp/fs/std.hpp>)
#include <asp/fs/std.hpp>
#endif

namespace paimon::paths {

// keep filesystem storage and native file I/O in std::filesystem, converting
// explicitly at the SDK boundary. Both directions preserve Unicode filenames.
inline std::string toUtf8(std::filesystem::path const& path) {
    auto const bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

inline std::filesystem::path toStdPath(std::filesystem::path const& path) {
    return path;
}

#if __has_include(<asp/fs/std.hpp>)
inline std::filesystem::path toStdPath(asp::fs::PathView path) {
    return path.toStdPath().unwrap();
}

inline asp::fs::Path toSdkPath(std::filesystem::path const& path) {
    return asp::fs::Path::fromStdPath(path);
}

inline asp::fs::Path toSdkPath(std::string const& path) {
    return asp::fs::Path(path);
}

inline asp::fs::Path toSdkPath(char const* path) {
    return asp::fs::Path(path);
}

inline asp::fs::Path toSdkPath(asp::fs::PathView path) {
    return path.toPath();
}

inline std::string toUtf8(asp::fs::PathView path) {
    return std::string(path.string());
}
#else
inline std::filesystem::path toSdkPath(std::filesystem::path const& path) {
    return path;
}
#endif

} // namespace paimon::paths
