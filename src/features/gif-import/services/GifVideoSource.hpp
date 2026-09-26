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

// decodeVideo progress mailbox; loader thread writes, UI reads.
struct VideoProgress {
    std::atomic<bool> cancelled = false;
    std::atomic<int> framesSeen = 0;
    std::atomic<int> framesKept = 0;
};

// Spreads captures across the video length and returns them RGBA as if from a
// GIF. Blocks while decoding, so it runs on the loader thread.
// Non-null partialOut reports stall/deadline trims.
std::shared_ptr<SourceAnimation> decodeVideo(
    std::filesystem::path const& path,
    int maxFrames,
    std::string& error,
    double maxDurationSeconds = 0.0,
    bool* partialOut = nullptr,
    VideoProgress* progress = nullptr
);

} // namespace paimon::gifimport
