#include "GifVideoSource.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../video/VideoDecoder.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>

#include <libyuv/convert_argb.h>
#include <libyuv/scale.h>

#include <algorithm>
#include <bit>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace paimon::gifimport {

namespace {

constexpr auto kStallTimeout = std::chrono::seconds(12);
constexpr size_t kFrameBudget = 96 * 1024 * 1024;

std::string extensionOf(std::filesystem::path const& path) {
    auto extension = geode::utils::string::pathToString(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// phone clips store landscape frames plus a display rotation flag.
void rotateRgba(std::vector<std::uint8_t>& rgba, int w, int h, int rotation) {
    if (rotation != 90 && rotation != 180 && rotation != 270) return;
    std::vector<std::uint8_t> dst(rgba.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::size_t d;
            if (rotation == 90) d = static_cast<std::size_t>(x) * h + (h - 1 - y);
            else if (rotation == 270) d = static_cast<std::size_t>(w - 1 - x) * h + y;
            else d = static_cast<std::size_t>(h - 1 - y) * w + (w - 1 - x);
            std::memcpy(&dst[d * 4], &rgba[(static_cast<std::size_t>(y) * w + x) * 4], 4);
        }
    }
    rgba = std::move(dst);
}

bool convertFrame(
    VideoFrame const& frame,
    int outputWidth,
    int outputHeight,
    VideoColorMatrix matrix,
    bool fullRange,
    int rotation,
    std::vector<std::uint8_t>& rgba
) {
    if (frame.width <= 0 || frame.height <= 0 || !frame.planeY || !frame.planeCb || !frame.planeCr) return false;
    auto const* constants = matrix == VideoColorMatrix::BT709
        ? (fullRange ? &libyuv::kYvuF709Constants : &libyuv::kYvuH709Constants)
        : (fullRange ? &libyuv::kYvuJPEGConstants : &libyuv::kYvuI601Constants);
    auto convert = [&](uint8_t const* y, int strideY, uint8_t const* u, int strideU, uint8_t const* v, int strideV) {
        // swapped chroma and yvu constants produce libyuv's abgr (rgba bytes).
        return libyuv::I420ToARGBMatrix(y, strideY, v, strideV, u, strideU,
            rgba.data(), outputWidth * 4, constants, outputWidth, outputHeight) == 0;
    };
    rgba.assign(static_cast<std::size_t>(outputWidth) * outputHeight * 4, 0);
    if (frame.width == outputWidth && frame.height == outputHeight) {
        if (!convert(
            frame.planeY, frame.strideY, frame.planeCb, frame.strideCb,
            frame.planeCr, frame.strideCr)) return false;
        rotateRgba(rgba, outputWidth, outputHeight, rotation);
        return true;
    }

    int const uvWidth = (outputWidth + 1) / 2;
    int const uvHeight = (outputHeight + 1) / 2;
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(outputWidth) * outputHeight);
    std::vector<std::uint8_t> blue(static_cast<std::size_t>(uvWidth) * uvHeight);
    std::vector<std::uint8_t> red(static_cast<std::size_t>(uvWidth) * uvHeight);
    if (libyuv::I420Scale(
        frame.planeY, frame.strideY, frame.planeCb, frame.strideCb,
        frame.planeCr, frame.strideCr, frame.width, frame.height,
        luma.data(), outputWidth, blue.data(), uvWidth, red.data(), uvWidth,
        outputWidth, outputHeight, libyuv::kFilterBox) != 0) return false;
    if (!convert(luma.data(), outputWidth, blue.data(), uvWidth, red.data(), uvWidth)) return false;
    rotateRgba(rgba, outputWidth, outputHeight, rotation);
    return true;
}

// thins to half keeping even frames, widening the covered stride.
void thinCaptured(std::vector<SourceFrame>& frames, std::vector<double>& stamps) {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < frames.size(); i += 2) {
        if (kept != i) {
            frames[kept] = std::move(frames[i]);
            stamps[kept] = stamps[i];
        }
        ++kept;
    }
    frames.resize(kept);
    stamps.resize(kept);
}

// headerless tail delay: median of the real gaps.
double medianGap(std::vector<double> const& stamps, double fallback) {
    if (stamps.size() < 2) return fallback;
    std::vector<double> gaps;
    gaps.reserve(stamps.size() - 1);
    for (std::size_t i = 1; i < stamps.size(); ++i) gaps.push_back(stamps[i] - stamps[i - 1]);
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    double const median = gaps[gaps.size() / 2];
    return median > 0.0 ? median : fallback;
}

} // namespace

bool isVideoFile(std::filesystem::path const& path) {
    static constexpr std::array kExtensions{
        ".mp4", ".mov", ".m4v", ".mpg", ".mpeg", ".avi", ".wmv", ".mkv", ".webm"
    };
    auto const extension = extensionOf(path);
    return std::find(kExtensions.begin(), kExtensions.end(), extension) != kExtensions.end();
}

std::shared_ptr<SourceAnimation> decodeVideo(
    std::filesystem::path const& path,
    int maxFrames,
    std::string& error,
    double maxDurationSeconds,
    bool* partialOut,
    VideoProgress* progress
) {
    if (partialOut) *partialOut = false;
    auto decoder = IVideoDecoder::create(geode::utils::string::pathToString(path));
    if (!decoder) {
#if defined(USE_MEDIA_NDK)
        error = "Este dispositivo no pudo abrir el video (codec no soportado o archivo danado).";
#elif defined(USE_AV_FOUNDATION)
        error = "No se pudo abrir el video (formato no soportado o archivo danado).";
#elif defined(USE_MEDIA_FOUNDATION)
        error = "No se pudo abrir el video. Si es HEVC/H.265 instala la extension HEVC, o el archivo esta danado.";
#else
        error = "No se pudo abrir el video (sin decodificador para este formato o archivo danado).";
#endif
        return nullptr;
    }

    double const reportedDuration = decoder->getDuration();
    bool const finiteDuration = (std::bit_cast<std::uint64_t>(reportedDuration) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
    if (maxDurationSeconds > 0.0 &&
        (!finiteDuration || !(reportedDuration > 0.0) || reportedDuration > maxDurationSeconds)) {
        error = "El video debe tener duracion conocida y no superar 30 segundos.";
        return nullptr;
    }
    double const duration = finiteDuration && reportedDuration > 0.0 ? reportedDuration : 0.0;
    int const sourceWidth = decoder->getWidth();
    int const sourceHeight = decoder->getHeight();
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        error = "El video no expone un tamano de imagen valido.";
        return nullptr;
    }

    int const rotation = ((decoder->getRotationDegrees() % 360) + 360) % 360;
    // sideways portrait: landscape frame plus rotation flag.
    bool const portrait = rotation == 90 || rotation == 270;
    int const codedWidth = portrait ? sourceHeight : sourceWidth;
    int const codedHeight = portrait ? sourceWidth : sourceHeight;

    int const longest = std::max(codedWidth, codedHeight);
    double const shrink = longest > kMaxVideoSide
        ? static_cast<double>(kMaxVideoSide) / longest : 1.0;
    // align the output sides with the 2x2 chroma grid.
    int const outputWidth = std::max(2, static_cast<int>(std::lround(codedWidth * shrink)) & ~1);
    int const outputHeight = std::max(2, static_cast<int>(std::lround(codedHeight * shrink)) & ~1);

    // matrix is measured native: the decoder may come downscaled.
    int const nativeWidth = decoder->getNativeWidth();
    int const nativeHeight = decoder->getNativeHeight();
    bool const wideGamut = (nativeWidth > 0 ? nativeWidth : sourceWidth) >= 1280 ||
        (nativeHeight > 0 ? nativeHeight : sourceHeight) >= 720;
    // hd is bt.709; reading it as bt.601 shifts colors.
    VideoColorMatrix matrix = decoder->getColorMatrix();
    if (matrix == VideoColorMatrix::Auto) {
        matrix = wideGamut ? VideoColorMatrix::BT709 : VideoColorMatrix::BT601;
    }
    bool const fullRange = decoder->isFullRange();
    auto const frameBytes = static_cast<size_t>(outputWidth) * outputHeight * 4;
    int const wanted = std::min(std::clamp(maxFrames, 1, 120), static_cast<int>(kFrameBudget / frameBytes));
    double step = duration > 0.1 ? duration / wanted : 0.0;

    auto animation = std::make_shared<SourceAnimation>();
    animation->width = outputWidth;
    animation->height = outputHeight;
    std::vector<double> stamps;

    decoder->startDecoding();
    double nextWanted = 0.0;
    auto lastFrame = std::chrono::steady_clock::now();
    auto deadline = lastFrame + std::chrono::seconds(45);
    bool stalled = false;
    // corrupt pts is skipped; the cap stops endless spinning.
    int badPtsStreak = 0;
    constexpr int kMaxBadPtsStreak = 600;
    bool aborted = false;
    bool conversionFailed = false;
    // decode through eos: stopping at full skews to the start when the header
    // undercounts; the doubling below thins to cover the whole range.
    for (;;) {
        if (progress && progress->cancelled.load(std::memory_order_relaxed)) { aborted = true; break; }
        if (paimon::isRuntimeShuttingDown()) { aborted = true; break; }
        if (std::chrono::steady_clock::now() > deadline) { stalled = true; break; }
        auto const* frame = decoder->peekFrame();
        if (!frame) {
            if (decoder->isFinished()) break;
            if (std::chrono::steady_clock::now() - lastFrame > kStallTimeout) { stalled = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        lastFrame = std::chrono::steady_clock::now();
        // headers may undercount; the ceiling yields to what's accepted.
        double const ptsCeil = stamps.empty()
            ? duration + 1.0
            : std::max(duration + 1.0, stamps.back() + 30.0);
        if ((std::bit_cast<std::uint64_t>(frame->pts) & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ||
            frame->pts < 0.0 || (maxDurationSeconds > 0.0 && frame->pts > ptsCeil)) {
            decoder->releaseFrame();
            if (++badPtsStreak > kMaxBadPtsStreak) { stalled = true; break; }
            continue;
        }
        badPtsStreak = 0;
        // no header step: start from the observed pace.
        if (step <= 0.0 && stamps.size() > 1) {
            step = medianGap(stamps, 0.04);
            nextWanted = stamps.back() + step;
        }
        // undercounted header: video outruns the plan; widen the step and thin
        // captures to keep covering the whole range.
        while (step > 0.0 && !stamps.empty() &&
               frame->pts > stamps.front() + step * wanted &&
               animation->frames.size() > 1) {
            step *= 2.0;
            thinCaptured(animation->frames, stamps);
            nextWanted = stamps.back() + step;
        }
        if (static_cast<int>(animation->frames.size()) < wanted &&
            frame->pts + 1e-6 >= nextWanted) {
            SourceFrame captured;
            if (!convertFrame(*frame, outputWidth, outputHeight, matrix, fullRange, rotation, captured.rgba)) {
                decoder->releaseFrame();
                conversionFailed = true;
                break;
            }
            animation->frames.push_back(std::move(captured));
            stamps.push_back(frame->pts);
            nextWanted = step > 0.0 ? nextWanted + step : frame->pts;
        }
        decoder->releaseFrame();
        if (progress) {
            progress->framesSeen.fetch_add(1, std::memory_order_relaxed);
            progress->framesKept.store(
                static_cast<int>(animation->frames.size()), std::memory_order_relaxed);
        }
    }
    decoder->stopDecoding();
    if (aborted) return nullptr;
    if (conversionFailed) {
        error = "No se pudo convertir un fotograma del video.";
        return nullptr;
    }
    if (animation->frames.empty()) {
        if (maxDurationSeconds > 0.0 && stalled) {
            error = "El decodificador no pudo completar el video.";
        } else {
            error = "No se pudo decodificar ningun fotograma del video.";
        }
        return nullptr;
    }
    if (maxDurationSeconds > 0.0 && stalled) {
        geode::log::warn(
            "[GifImport] video parcial: {} frames antes del corte",
            animation->frames.size());
        if (partialOut) *partialOut = true;
    }

    // pace comes from real timestamps.
    double const lastStep = medianGap(stamps, step > 0.0 ? step : 0.04);
    for (std::size_t i = 0; i < animation->frames.size(); ++i) {
        double const next = i + 1 < stamps.size() ? stamps[i + 1] - stamps[i] : lastStep;
        if (maxDurationSeconds > 0.0) {
            // rounding endpoints preserves fractional frame periods.
            double const startMs = i == 0 ? 0.0 : std::round(stamps[i] * 1000.0);
            double const endMs = std::round((stamps[i] + next) * 1000.0);
            double const delayMs = std::isfinite(endMs - startMs) ? endMs - startMs : 40.0;
            animation->frames[i].delayMs = static_cast<int>(std::lround(std::clamp(delayMs, 1.0, 30000.0)));
        } else {
            double const delayMs = std::isfinite(next) ? std::clamp(next * 1000.0, 20.0, 2000.0) : 40.0;
            animation->frames[i].delayMs = static_cast<int>(std::lround(delayMs));
        }
    }
    geode::log::info(
        "[GifImport] video {}x{} -> {} frames de {}x{}",
        sourceWidth, sourceHeight, animation->frames.size(), outputWidth, outputHeight);
    return animation;
}

} // namespace paimon::gifimport
