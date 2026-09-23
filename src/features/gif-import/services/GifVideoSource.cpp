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

std::string extensionOf(std::filesystem::path const& path) {
    auto extension = geode::utils::string::pathToString(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// Phone clips store landscape frames plus a display rotation flag.
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

void convertFrame(
    VideoFrame const& frame,
    int outputWidth,
    int outputHeight,
    VideoColorMatrix matrix,
    bool fullRange,
    int rotation,
    std::vector<std::uint8_t>& rgba
) {
    auto const convert = fullRange ? libyuv::J420ToABGR
        : (matrix == VideoColorMatrix::BT709 ? libyuv::H420ToABGR : libyuv::I420ToABGR);
    rgba.assign(static_cast<std::size_t>(outputWidth) * outputHeight * 4, 0);
    if (frame.width == outputWidth && frame.height == outputHeight) {
        convert(
            frame.planeY, frame.strideY, frame.planeCb, frame.strideCb,
            frame.planeCr, frame.strideCr, rgba.data(), outputWidth * 4,
            outputWidth, outputHeight);
        rotateRgba(rgba, outputWidth, outputHeight, rotation);
        return;
    }

    int const uvWidth = (outputWidth + 1) / 2;
    int const uvHeight = (outputHeight + 1) / 2;
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(outputWidth) * outputHeight);
    std::vector<std::uint8_t> blue(static_cast<std::size_t>(uvWidth) * uvHeight);
    std::vector<std::uint8_t> red(static_cast<std::size_t>(uvWidth) * uvHeight);
    libyuv::I420Scale(
        frame.planeY, frame.strideY, frame.planeCb, frame.strideCb,
        frame.planeCr, frame.strideCr, frame.width, frame.height,
        luma.data(), outputWidth, blue.data(), uvWidth, red.data(), uvWidth,
        outputWidth, outputHeight, libyuv::kFilterBox);
    convert(
        luma.data(), outputWidth, blue.data(), uvWidth, red.data(), uvWidth,
        rgba.data(), outputWidth * 4, outputWidth, outputHeight);
    rotateRgba(rgba, outputWidth, outputHeight, rotation);
}

// Ralea a la mitad quedandose con los pares para ensanchar el paso cubierto.
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

// Ultimo delay sin cabecera: mediana de los saltos reales.
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

    double const duration = decoder->getDuration();
    bool const finiteDuration = (std::bit_cast<std::uint64_t>(duration) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
    if (maxDurationSeconds > 0.0 &&
        (!finiteDuration || !(duration > 0.0) || duration > maxDurationSeconds)) {
        error = "El video debe tener duracion conocida y no superar 30 segundos.";
        return nullptr;
    }
    int const sourceWidth = decoder->getWidth();
    int const sourceHeight = decoder->getHeight();
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        error = "El video no expone un tamano de imagen valido.";
        return nullptr;
    }

    int const rotation = ((decoder->getRotationDegrees() % 360) + 360) % 360;
    // Vertical tumbado: el frame viene apaisado con flag de giro.
    bool const portrait = rotation == 90 || rotation == 270;
    int const codedWidth = portrait ? sourceHeight : sourceWidth;
    int const codedHeight = portrait ? sourceWidth : sourceHeight;

    int const longest = std::max(codedWidth, codedHeight);
    double const shrink = longest > kMaxVideoSide
        ? static_cast<double>(kMaxVideoSide) / longest : 1.0;
    // Croma va de 2 en 2: un lado impar rompe el escalado en libyuv.
    int const outputWidth = std::max(2, static_cast<int>(std::lround(codedWidth * shrink)) & ~1);
    int const outputHeight = std::max(2, static_cast<int>(std::lround(codedHeight * shrink)) & ~1);

    // La matriz se mide en nativo: el decoder puede venir reducido.
    int const nativeWidth = decoder->getNativeWidth();
    int const nativeHeight = decoder->getNativeHeight();
    bool const wideGamut = (nativeWidth > 0 ? nativeWidth : sourceWidth) >= 1280 ||
        (nativeHeight > 0 ? nativeHeight : sourceHeight) >= 720;
    // HD es BT.709; leerla como BT.601 vira los colores.
    VideoColorMatrix matrix = decoder->getColorMatrix();
    if (matrix == VideoColorMatrix::Auto) {
        matrix = wideGamut ? VideoColorMatrix::BT709 : VideoColorMatrix::BT601;
    }
    bool const fullRange = decoder->isFullRange();
    int const wanted = std::clamp(maxFrames, 1, 120);
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
    // PTS corrupto se salta; el tope evita girar sin fin.
    int badPtsStreak = 0;
    constexpr int kMaxBadPtsStreak = 600;
    bool aborted = false;
    // Se decodifica hasta EOS: parar al llenar sesga al inicio si la cabecera
    // infravalora; el doblado de abajo ralea para cubrir todo el rango.
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
        // La cabecera puede infravalorar; el techo cede con lo ya aceptado.
        double const ptsCeil = stamps.empty()
            ? duration + 1.0
            : std::max(duration + 1.0, stamps.back() + 30.0);
        if (maxDurationSeconds > 0.0 &&
            ((std::bit_cast<std::uint64_t>(frame->pts) & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ||
             frame->pts < 0.0 || frame->pts > ptsCeil)) {
            decoder->releaseFrame();
            if (++badPtsStreak > kMaxBadPtsStreak) { stalled = true; break; }
            continue;
        }
        badPtsStreak = 0;
        // Sin paso de cabecera se arranca del ritmo observado.
        if (step <= 0.0 && stamps.size() > 1) {
            step = medianGap(stamps, 0.04);
            nextWanted = stamps.back() + step;
        }
        // Cabecera infravalorada: el video supera el plan; se ensancha el paso
        // y se ralea lo capturado para seguir cubriendo todo el rango.
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
            convertFrame(*frame, outputWidth, outputHeight, matrix, fullRange, rotation, captured.rgba);
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
    // Cancelado o cerrando: se descarta en silencio, sin error ni parcial.
    if (aborted) return nullptr;
    if (animation->frames.empty()) {
        if (maxDurationSeconds > 0.0 && stalled) {
            error = "El decodificador no pudo completar el video.";
        } else {
            error = "No se pudo decodificar ningun fotograma del video.";
        }
        return nullptr;
    }
    if (maxDurationSeconds > 0.0 && stalled) {
        // Corte tardio: se devuelve lo capturado con aviso.
        geode::log::warn(
            "[GifImport] video parcial: {} frames antes del corte",
            animation->frames.size());
        if (partialOut) *partialOut = true;
    }

    // El ritmo sale de los timestamps reales.
    double const lastStep = medianGap(stamps, step > 0.0 ? step : 0.04);
    for (std::size_t i = 0; i < animation->frames.size(); ++i) {
        double const next = i + 1 < stamps.size() ? stamps[i + 1] - stamps[i] : lastStep;
        if (maxDurationSeconds > 0.0) {
            // Redondear acumulado: evita que 60fps degrade a 50fps por el clamp.
            long const startMs = i == 0 ? 0 : std::lround(stamps[i] * 1000.0);
            long const endMs = std::lround((stamps[i] + next) * 1000.0);
            animation->frames[i].delayMs = static_cast<int>(std::clamp(endMs - startMs, 1L, 30000L));
        } else {
            animation->frames[i].delayMs = std::clamp(
                static_cast<int>(std::lround(next * 1000.0)), 20, 2000);
        }
    }
    geode::log::info(
        "[GifImport] video {}x{} -> {} frames de {}x{}",
        sourceWidth, sourceHeight, animation->frames.size(), outputWidth, outputHeight);
    return animation;
}

} // namespace paimon::gifimport
