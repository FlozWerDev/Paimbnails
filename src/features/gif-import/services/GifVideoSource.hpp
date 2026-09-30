#pragma once

#include "../GifImportTypes.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace paimon::gifimport {

bool isVideoFile(std::filesystem::path const& path);

// 768 keeps the thin edge; more resolution only spends memory.
inline constexpr int kMaxVideoSide = 768;

// decodevideo progress mailbox; loader thread writes, ui reads.
struct VideoProgress {
    std::atomic<bool> cancelled = false;
    std::atomic<int> framesSeen = 0;
    std::atomic<int> framesKept = 0;
};

// loader thread only: decoding blocks. partial output reports deadline or stall trims.
std::shared_ptr<SourceAnimation> decodeVideo(
    std::filesystem::path const& path,
    int maxFrames,
    std::string& error,
    double maxDurationSeconds = 0.0,
    bool* partialOut = nullptr,
    VideoProgress* progress = nullptr
);

} // namespace paimon::gifimport
