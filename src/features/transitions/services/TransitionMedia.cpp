#include "TransitionMedia.hpp"
#include "TransitionTimeline.hpp"
#include "../../gif-import/services/GifVideoSource.hpp"
#include "../../../utils/GIFDecoder.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/stb_image.h"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cstring>
#include <libyuv/scale_argb.h>

using namespace geode::prelude;
namespace paimon::transitions {
namespace {
constexpr std::size_t kBudget = 96 * 1024 * 1024;
constexpr int kPage = 2048;
struct Page { int width = 0, height = 0; std::vector<std::uint8_t> rgba; };
struct Decoded {
    std::string manifest, error;
    int width = 0, height = 0, columns = 0, perPage = 0;
    std::vector<int> ends;
    std::vector<Page> pages;
};
std::unordered_map<std::string, std::shared_ptr<TransitionMedia>> cache;
std::unordered_map<std::string, std::vector<MediaCallback>> pending;

std::vector<std::uint8_t> readBytes(std::filesystem::path const& path, std::size_t limit = kBudget) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > limit) return {};
    // Bound the allocation even if an external editor grows the file after stat.
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) return {};
    return bytes;
}

Page readImage(std::filesystem::path const& path) {
    Page page;
    auto bytes = readBytes(path);
    int channels = 0;
    if (bytes.empty() || !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &page.width, &page.height, &channels)
        || page.width < 1 || page.height < 1 || page.width > 4096 || page.height > 4096) return {};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &page.width, &page.height, &channels, 4),
        stbi_image_free);
    if (!pixels) return {};
    page.rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(page.width) * page.height * 4);
    return page;
}
bool loadManifest(std::filesystem::path const& path, Decoded& out) {
    auto bytes = readBytes(path, 65536);
    if (bytes.empty()) return false;
    auto parsed = matjson::parse(std::string(bytes.begin(), bytes.end()));
    if (!parsed) return false;
    auto const& json = parsed.unwrap();
    if (json["version"].asInt().unwrapOr(0) != 1) return false;
    auto width = json["width"].asInt().unwrapOr(0);
    auto height = json["height"].asInt().unwrapOr(0);
    if (width < 1 || height < 1 || width > 1024 || height > 1024) return false;
    out.width = static_cast<int>(width); out.height = static_cast<int>(height);
    out.columns = kPage / (out.width + 2);
    out.perPage = out.columns * (kPage / (out.height + 2));
    auto delays = json["delays"].asArray().unwrapOr(std::vector<matjson::Value>{});
    if (delays.empty() || delays.size() > 240) return false;
    int total = 0;
    for (auto const& delay : delays) {
        auto ms = delay.asInt().unwrapOr(0);
        if (ms < 1 || ms > 30000 || total + ms > 30000) return false;
        total += static_cast<int>(ms);
        out.ends.push_back(total);
    }
    std::size_t budget = 0;
    int pages = (static_cast<int>(delays.size()) + out.perPage - 1) / out.perPage;
    for (int i = 0; i < pages; ++i) {
        auto page = readImage(path.parent_path() / ("page-" + std::to_string(i) + ".png"));
        int count = std::min(out.perPage, static_cast<int>(delays.size()) - i * out.perPage);
        int expectedW = std::min(count, out.columns) * (out.width + 2);
        int expectedH = ((count + out.columns - 1) / out.columns) * (out.height + 2);
        if (page.rgba.empty() || page.width != expectedW || page.height != expectedH) return false;
        budget += page.rgba.size();
        if (budget > kBudget) return false;
        out.pages.push_back(std::move(page));
    }
    out.manifest = utils::string::pathToString(path);
    return true;
}
Decoded importMedia(std::string const& source, std::filesystem::path const& root) {
    Decoded out;
#if defined(GEODE_IS_WINDOWS)
    auto path = std::filesystem::path(utils::string::utf8ToWide(source));
#else
    auto path = std::filesystem::path(source);
#endif
    if (path.extension() == ".pttransition") {
        if (!loadManifest(path, out)) { out = {}; out.error = "Sheet invalido o incompleto. Importa el original de nuevo."; }
        return out;
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kBudget) { out.error = "Archivo ausente o mayor de 96 MB."; return out; }
    auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) { out.error = "No se pudo leer el archivo."; return out; }
    auto identity = source + std::to_string(static_cast<unsigned long long>(size)) +
        std::to_string(static_cast<long long>(stamp.time_since_epoch().count())) + "sheet-export-v2";
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : identity) { hash ^= c; hash *= 1099511628211ull; }
    auto dir = root / fmt::format("{:016x}", hash);
    auto manifest = dir / "animation.pttransition";
    if (loadManifest(manifest, out)) return out;
    out = {};
    auto animation = std::make_shared<gifimport::SourceAnimation>();
    if (gifimport::isVideoFile(path)) {
        animation = gifimport::decodeVideo(path, 120, out.error, 30.0);
        if (!animation) return out;
    } else {
        auto bytes = readBytes(path);
        if (GIFDecoder::isGIF(bytes.data(), bytes.size())) {
            int w = 0, h = 0;
            if (!GIFDecoder::getDimensions(bytes.data(), bytes.size(), w, h) || w < 1 || h < 1 || w > 4096 || h > 4096) {
                out.error = "Dimensiones GIF invalidas (maximo 4096)."; return out;
            }
            auto frameBytes = static_cast<std::size_t>(w) * h * 4;
            int limit = static_cast<int>(std::min<std::size_t>(241, kBudget / frameBytes));
            if (limit < 2) { out.error = "GIF demasiado grande para convertir."; return out; }
            auto gif = GIFDecoder::decode(bytes.data(), bytes.size(), limit);
            if (static_cast<int>(gif.frames.size()) >= limit) {
                out.error = "GIF supera el limite de memoria/240 frames. Reduce resolucion o duracion."; return out;
            }
            animation->width = w; animation->height = h;
            for (auto& frame : gif.frames) animation->frames.push_back({std::max(10, frame.delayMs), std::move(frame.pixels)});
        } else {
            auto page = readImage(path);
            animation->width = page.width; animation->height = page.height;
            if (!page.rgba.empty()) animation->frames.push_back({1000, std::move(page.rgba)});
        }
    }
    if (animation->frames.empty() || animation->width < 1 || animation->height < 1) { out.error = "No se pudo decodificar el medio."; return out; }
    int sourceW = animation->width, sourceH = animation->height;
    float scale = std::min(1.f, 1024.f / std::max(sourceW, sourceH));
    out.width = std::max(1, static_cast<int>(sourceW * scale));
    out.height = std::max(1, static_cast<int>(sourceH * scale));
    out.columns = kPage / (out.width + 2);
    out.perPage = out.columns * (kPage / (out.height + 2));
    auto json = matjson::makeObject({});
    json["version"] = 1; json["width"] = out.width; json["height"] = out.height;
    auto delays = matjson::Value::array();
    int total = 0;
    for (auto const& frame : animation->frames) {
        if (frame.rgba.size() != static_cast<std::size_t>(sourceW) * sourceH * 4 || frame.delayMs < 1 || frame.delayMs > 30000) {
            out.error = "Fotograma invalido."; return out;
        }
        total += frame.delayMs;
        if (total > 30000) { out.error = "La transicion no puede superar 30 segundos."; return out; }
        delays.push(frame.delayMs); out.ends.push_back(total);
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) { out.error = "No se pudo crear el cache de transiciones."; return out; }
    std::size_t budget = 0;
    for (std::size_t first = 0; first < animation->frames.size(); first += out.perPage) {
        int count = static_cast<int>(std::min<std::size_t>(out.perPage, animation->frames.size() - first));
        Page page;
        page.width = std::min(count, out.columns) * (out.width + 2);
        page.height = ((count + out.columns - 1) / out.columns) * (out.height + 2);
        budget += static_cast<std::size_t>(page.width) * page.height * 4;
        if (budget > kBudget) { out.error = "Las hojas superan 96 MB. Reduce el medio."; return out; }
        page.rgba.resize(static_cast<std::size_t>(page.width) * page.height * 4);
        for (int i = 0; i < count; ++i) {
            auto& frame = animation->frames[first + i];
            // Filter premultiplied pixels so transparent borders cannot inject
            // their hidden RGB into the resized image.
            for (std::size_t p = 0; p < frame.rgba.size(); p += 4)
                for (int c = 0; c < 3; ++c) frame.rgba[p + c] = (frame.rgba[p + c] * frame.rgba[p + 3] + 127) / 255;
            std::vector<std::uint8_t> resized;
            auto const* pixels = frame.rgba.data();
            if (sourceW != out.width || sourceH != out.height) {
                resized.resize(static_cast<std::size_t>(out.width) * out.height * 4);
                if (libyuv::ARGBScale(pixels, sourceW * 4, sourceW, sourceH,
                    resized.data(), out.width * 4, out.width, out.height, libyuv::kFilterBox) != 0) {
                    out.error = "No se pudo redimensionar el medio."; return out;
                }
                pixels = resized.data();
            }
            int x0 = (i % out.columns) * (out.width + 2), y0 = (i / out.columns) * (out.height + 2);
            // Extrude one texel around every frame to prevent linear-filter bleeding.
            for (int y = -1; y <= out.height; ++y) for (int x = -1; x <= out.width; ++x) {
                int sx = std::clamp(x, 0, out.width - 1);
                int sy = std::clamp(y, 0, out.height - 1);
                auto const* pixel = pixels + (static_cast<std::size_t>(sy) * out.width + sx) * 4;
                auto* dest = page.rgba.data() + (static_cast<std::size_t>(y0 + y + 1) * page.width + x0 + x + 1) * 4;
                std::memcpy(dest, pixel, 4);
            }
            std::vector<std::uint8_t>().swap(frame.rgba);
        }
        auto filename = dir / ("page-" + std::to_string(out.pages.size()) + ".png");
        if (!ImageConverter::saveRGBAToPNG(page.rgba.data(), page.width, page.height, filename)) {
            out.error = "No se pudo guardar la hoja."; return out;
        }
        out.pages.push_back(std::move(page));
    }
    json["delays"] = delays;
    auto temp = dir / "animation.tmp";
    if (!utils::file::writeString(temp, json.dump())) { out.error = "No se pudo guardar el indice."; return out; }
    // Publish only after every page is complete. A missing manifest is rebuilt.
    std::filesystem::remove(manifest, ec);
    std::filesystem::rename(temp, manifest, ec);
    if (ec) { out.error = "No se pudo publicar el indice."; return out; }
    out.manifest = utils::string::pathToString(manifest);
    return out;
}
struct Job { std::string path; std::filesystem::path root; };
class ImportWorker {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::atomic<bool> stopping{false};
    std::thread worker;
public:
    ImportWorker() : worker([this] {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping) return;
                job = std::move(jobs.front()); jobs.pop_front();
            }
            auto decoded = std::make_shared<Decoded>();
            try { *decoded = importMedia(job.path, job.root); }
            catch (std::exception const& e) { decoded->error = e.what(); }
            auto delivered = std::make_shared<std::atomic<bool>>(false);
            Loader::get()->queueInMainThread([path = job.path, decoded, delivered] {
                struct Completion {
                    std::shared_ptr<std::atomic<bool>> flag;
                    ~Completion() { flag->store(true); }
                } completion{delivered};
                std::shared_ptr<TransitionMedia> media;
                auto existing = cache.find(decoded->manifest);
                if (decoded->error.empty() && existing != cache.end()) media = existing->second;
                if (decoded->error.empty() && !media) {
                    std::size_t incoming = 0, total = 0;
                    for (auto const& page : decoded->pages) incoming += page.rgba.size();
                    for (auto const& entry : cache) total += entry.second->bytes;
                    for (auto it = cache.begin(); it != cache.end() && total + incoming > 2 * kBudget;) {
                        if (it->second.use_count() == 1) { total -= it->second->bytes; it = cache.erase(it); }
                        else ++it;
                    }
                    if (total + incoming > 2 * kBudget) decoded->error = "Demasiadas transiciones en uso.";
                }
                if (decoded->error.empty() && !media) {
                    media = std::make_shared<TransitionMedia>();
                    media->manifest = decoded->manifest;
                    media->width = decoded->width; media->height = decoded->height;
                    media->columns = decoded->columns; media->perPage = decoded->perPage;
                    media->endsMs = decoded->ends;
                    for (auto const& page : decoded->pages) {
                        auto* texture = new CCTexture2D();
                        bool ok = texture->initWithData(page.rgba.data(), kCCTexture2DPixelFormat_RGBA8888,
                            page.width, page.height, CCSize(page.width, page.height));
                        if (ok) {
                            texture->setAntiAliasTexParameters();
                            media->pages.emplace_back(texture);
                            media->bytes += page.rgba.size();
                        }
                        texture->release();
                        if (!ok) { media.reset(); decoded->error = "La GPU no pudo cargar las hojas."; break; }
                    }
                }
                if (media) cache[media->manifest] = media;
                auto callbacks = std::move(pending[path]); pending.erase(path);
                for (auto& callback : callbacks) if (callback) callback(media, decoded->error);
                if (!media) log::warn("[Transitions] Media import: {}", decoded->error);
            });
            // At most one decoded animation may wait for a GPU upload, even
            // when a large legacy script requests many assets at startup.
            while (!delivered->load() && !stopping.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (stopping.load()) return;
        }
    }) {}
    ~ImportWorker() {
        { std::lock_guard lock(mutex); stopping = true; }
        cv.notify_one(); worker.join();
    }
    void enqueue(Job job) {
        { std::lock_guard lock(mutex); jobs.push_back(std::move(job)); }
        cv.notify_one();
    }
};
std::unordered_map<std::string, std::string> aliases;
} // namespace
std::shared_ptr<TransitionMedia> findTransitionMedia(std::string const& path) {
    auto alias = aliases.find(path);
    auto it = cache.find(alias == aliases.end() ? path : alias->second);
    return it == cache.end() ? nullptr : it->second;
}
void prepareTransitionMedia(std::string const& path, MediaCallback callback) {
    if (path.empty()) { if (callback) callback(nullptr, "Elige un medio primero."); return; }
    if (auto media = findTransitionMedia(path)) { if (callback) callback(media, {}); return; }
    auto [it, inserted] = pending.try_emplace(path);
    it->second.push_back([path, callback = std::move(callback)](auto media, auto error) {
        if (media) aliases[path] = media->manifest;
        if (callback) callback(std::move(media), std::move(error));
    });
    if (!inserted) return;
    static ImportWorker worker;
    worker.enqueue({path, Mod::get()->getSaveDir() / "transitions" / "sheets"});
}
void TransitionMedia::apply(CCSprite* sprite, double seconds) const {
    if (!sprite || pages.empty() || endsMs.empty()) return;
    auto index = frameAt(endsMs, std::max(0.0, seconds) * 1000.0);
    auto cell = index % perPage;
    auto* texture = pages[index / perPage].data();
    bool changedPage = sprite->getTexture() != texture;
    if (changedPage) sprite->setTexture(texture);
    CCRect rect((cell % columns) * (width + 2) + 1,
        (cell / columns) * (height + 2) + 1, width, height);
    if (changedPage || !sprite->getTextureRect().equals(rect)) sprite->setTextureRect(rect);
    sprite->setOpacityModifyRGB(true);
    sprite->setBlendFunc({GL_ONE, GL_ONE_MINUS_SRC_ALPHA});
}
} // namespace paimon::transitions
