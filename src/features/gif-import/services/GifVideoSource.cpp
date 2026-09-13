#include "GifVideoSource.hpp"

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
#include <thread>

namespace paimon::gifimport {

namespace {

// El importador nunca pasa de 320 celdas de lado, asi que traer el video a mas
// resolucion solo gasta memoria: sesenta capturas de un 1080p son medio giga.
constexpr int kMaxVideoSide = 512;
constexpr auto kStallTimeout = std::chrono::seconds(12);

std::string extensionOf(std::filesystem::path const& path) {
    auto extension = geode::utils::string::pathToString(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

void convertFrame(
    VideoFrame const& frame,
    int outputWidth,
    int outputHeight,
    bool wideGamut,
    std::vector<std::uint8_t>& rgba
) {
    // El reproductor ya trata la alta definicion como BT.709; leerla como BT.601
    // vira los verdes y apaga los rojos, y la paleta sale de estos pixeles.
    auto const convert = wideGamut ? libyuv::H420ToABGR : libyuv::I420ToABGR;
    rgba.assign(static_cast<std::size_t>(outputWidth) * outputHeight * 4, 0);
    if (frame.width == outputWidth && frame.height == outputHeight) {
        convert(
            frame.planeY, frame.strideY, frame.planeCb, frame.strideCb,
            frame.planeCr, frame.strideCr, rgba.data(), outputWidth * 4,
            outputWidth, outputHeight);
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
    double maxDurationSeconds
) {
    auto decoder = IVideoDecoder::create(geode::utils::string::pathToString(path));
    if (!decoder) {
        error = "Esta version de Windows no pudo abrir el video.";
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

    int const longest = std::max(sourceWidth, sourceHeight);
    double const shrink = longest > kMaxVideoSide
        ? static_cast<double>(kMaxVideoSide) / longest : 1.0;
    // Los planos de croma van de dos en dos pixeles, asi que un lado impar deja
    // media columna sin color y libyuv se sale del buffer al escalar.
    int const outputWidth = std::max(2, static_cast<int>(std::lround(sourceWidth * shrink)) & ~1);
    int const outputHeight = std::max(2, static_cast<int>(std::lround(sourceHeight * shrink)) & ~1);

    bool const wideGamut = sourceWidth >= 1280 || sourceHeight >= 720;
    int const wanted = std::clamp(maxFrames, 1, 120);
    double const step = duration > 0.1 ? duration / wanted : 0.0;

    auto animation = std::make_shared<SourceAnimation>();
    animation->width = outputWidth;
    animation->height = outputHeight;
    std::vector<double> stamps;

    decoder->startDecoding();
    double nextWanted = 0.0;
    auto lastFrame = std::chrono::steady_clock::now();
    auto deadline = lastFrame + std::chrono::seconds(45);
    bool stalled = false;
    while (static_cast<int>(animation->frames.size()) < wanted) {
        if (maxDurationSeconds > 0.0 && std::chrono::steady_clock::now() > deadline) { stalled = true; break; }
        auto const* frame = decoder->peekFrame();
        if (!frame) {
            if (decoder->isFinished() || decoder->isTerminal()) break;
            if (std::chrono::steady_clock::now() - lastFrame > kStallTimeout) { stalled = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        lastFrame = std::chrono::steady_clock::now();
        if (maxDurationSeconds > 0.0 &&
            ((std::bit_cast<std::uint64_t>(frame->pts) & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ||
             frame->pts < 0.0 || frame->pts > duration + 1.0)) {
            decoder->releaseFrame(); stalled = true; break;
        }
        if (frame->pts + 1e-6 >= nextWanted) {
            SourceFrame captured;
            convertFrame(*frame, outputWidth, outputHeight, wideGamut, captured.rgba);
            animation->frames.push_back(std::move(captured));
            stamps.push_back(frame->pts);
            nextWanted = step > 0.0 ? nextWanted + step : frame->pts;
        }
        decoder->releaseFrame();
    }
    bool terminal = decoder->isTerminal();
    decoder->stopDecoding();
    if (maxDurationSeconds > 0.0 && (stalled || terminal)) {
        error = "El decodificador no pudo completar el video.";
        return nullptr;
    }

    if (animation->frames.empty()) {
        error = "No se pudo decodificar ningun fotograma del video.";
        return nullptr;
    }

    // El ritmo sale de las marcas de tiempo reales, que es lo unico que sabe si el
    // video venia a 24, a 30 o con fotogramas repetidos.
    for (std::size_t i = 0; i < animation->frames.size(); ++i) {
        double const next = i + 1 < stamps.size()
            ? stamps[i + 1] - stamps[i]
            : (maxDurationSeconds > 0.0 ? std::max(.001, duration - stamps[i]) : (step > 0.0 ? step : 0.04));
        if (maxDurationSeconds > 0.0) {
            // Round cumulative timestamps, not each interval: 60fps must not
            // turn into 50fps through the editor importer's 20ms clamp.
            long const startMs = i == 0 ? 0 : std::lround(stamps[i] * 1000.0);
            long const endMs = std::lround((i + 1 < stamps.size() ? stamps[i + 1] : duration) * 1000.0);
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
