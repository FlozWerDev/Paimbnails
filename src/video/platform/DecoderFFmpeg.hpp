#pragma once

#include "../VideoDecoder.hpp"

#if defined(_WIN32)

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace paimon {

// Software FFmpeg subprocess pipe decoder used when running under Wine/Proton,
// where Media Foundation (winegstreamer) lacks H.264/HEVC decoders and falls
// back to a 320x240 SMPTE test pattern. Uses the mod's bundled ffmpeg.exe.
class DecoderFFmpeg final : public IVideoDecoder {
public:
    DecoderFFmpeg() = default;
    ~DecoderFFmpeg() override;

    static bool isWineEnvironment();
    static bool isFfmpegAvailable();
    static void ensureFfmpegAsyncIfNeeded();

    bool open(const std::string& path) override;
    void startDecoding() override;
    void stopDecoding() override;
    void seekTo(double seconds) override;

    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    int getNativeWidth() const override;
    int getNativeHeight() const override;
    VideoColorMatrix getColorMatrix() const override;
    bool isFullRange() const override;
    int getRotationDegrees() const override;
    bool isFinished() const override;

    bool skipFrame() override;
    double peekNextPTS() const override;
    double peekSecondPTS() const override;
    const Frame* peekFrame() override;
    void releaseFrame() override;

private:
    struct State;
    void closeInternal();

    std::shared_ptr<State> m_state;
    std::thread m_thread;
};

} // namespace paimon

#endif
