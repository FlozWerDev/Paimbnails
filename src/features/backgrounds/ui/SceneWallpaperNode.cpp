#include "SceneWallpaperNode.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include "../services/SceneJson.hpp"
#include "../services/SceneLoader.hpp"
#include "../services/ScenePackage.hpp"
#include "../services/WallpaperEngineManager.hpp"
#include "../services/WallpaperLayout.hpp"
#include "../../../utils/stb_image.h"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/HashKey.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../video/VideoPlayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace paimon::wallpaper {
namespace {

using namespace scenejson;

constexpr ccBlendFunc kNormalBlend{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
constexpr ccBlendFunc kAdditiveBlend{GL_SRC_ALPHA, GL_ONE};
// screen, multiply and brighten layers upload premultiplied (see SceneLoader).
constexpr ccBlendFunc kScreenBlend{GL_ONE_MINUS_DST_COLOR, GL_ONE};
constexpr ccBlendFunc kMultiplyBlend{GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA};
constexpr ccBlendFunc kBrightenBlend{GL_DST_COLOR, GL_ONE};
constexpr size_t kTextureCacheBudget = 384ull * 1024 * 1024;

ccBlendFunc blendFuncOf(SceneBlend blend) {
    switch (blend) {
        case SceneBlend::Additive: return kAdditiveBlend;
        case SceneBlend::Screen: return kScreenBlend;
        case SceneBlend::Multiply: return kMultiplyBlend;
        case SceneBlend::Brighten: return kBrightenBlend;
        default: return kNormalBlend;
    }
}

bool premultipliedBlend(SceneBlend blend) {
    return blend == SceneBlend::Screen || blend == SceneBlend::Multiply || blend == SceneBlend::Brighten;
}

// ---------------------------------------------------------------------------
// main-thread caches, all heap-owned: no CCTexture2D may destruct after the
// gl context is gone at exit.

struct TextureEntry {
    Ref<CCTexture2D> texture;
    // video layers keep no texture here: each node opens its own player.
    std::filesystem::path videoFile;
    std::vector<SceneTextureFrame> frames;
    int visibleW = 0, visibleH = 0;
    int sourceW = 0, sourceH = 0;
    SceneCoverage coverage;
    size_t bytes = 0;
    uint64_t lastUse = 0;
};

std::unordered_map<std::string, TextureEntry>& textureStore() {
    static auto* store = new std::unordered_map<std::string, TextureEntry>();
    return *store;
}
size_t& textureStoreBytes() { static size_t bytes = 0; return bytes; }
uint64_t& useCounter() { static uint64_t counter = 0; return counter; }

std::string textureKey(std::string const& rootKey, std::string const& path) { return rootKey + "|" + path; }

TextureEntry* textureGet(std::string const& key) {
    auto& store = textureStore();
    auto it = store.find(key);
    if (it == store.end()) return nullptr;
    it->second.lastUse = ++useCounter();
    return &it->second;
}

void textureErase(std::string const& key) {
    auto& store = textureStore();
    auto it = store.find(key);
    if (it == store.end()) return;
    textureStoreBytes() -= std::min(textureStoreBytes(), it->second.bytes);
    store.erase(it);
}

void texturePut(std::string const& key, TextureEntry entry) {
    textureErase(key);
    entry.lastUse = ++useCounter();
    textureStoreBytes() += entry.bytes;
    textureStore().emplace(key, std::move(entry));
    // LRU eviction; live nodes keep their own refs.
    auto& store = textureStore();
    while (textureStoreBytes() > kTextureCacheBudget && store.size() > 1) {
        auto oldest = store.end();
        for (auto it = store.begin(); it != store.end(); ++it) {
            if (it->first == key) continue;
            if (oldest == store.end() || it->second.lastUse < oldest->second.lastUse) oldest = it;
        }
        if (oldest == store.end()) break;
        textureStoreBytes() -= std::min(textureStoreBytes(), oldest->second.bytes);
        store.erase(oldest);
    }
}

std::unordered_map<std::string, SceneCoverage> coverageSnapshot(std::string const& rootKey) {
    std::unordered_map<std::string, SceneCoverage> snapshot;
    auto const prefix = rootKey + "|";
    for (auto const& [key, entry] : textureStore()) {
        if (key.starts_with(prefix)) snapshot.emplace(key.substr(prefix.size()), entry.coverage);
    }
    return snapshot;
}

// per-project scene descriptors (cpu-only, survive gl reloads). tiny lru.
std::unordered_map<std::string, std::pair<std::shared_ptr<SceneData const>, uint64_t>>& descriptorStore() {
    static auto* store = new std::unordered_map<std::string, std::pair<std::shared_ptr<SceneData const>, uint64_t>>();
    return *store;
}

std::shared_ptr<SceneData const> descriptorGet(std::string const& rootKey) {
    auto& store = descriptorStore();
    auto it = store.find(rootKey);
    if (it == store.end()) return nullptr;
    it->second.second = ++useCounter();
    return it->second.first;
}

void descriptorPut(std::string const& rootKey, std::shared_ptr<SceneData const> data) {
    if (!data) return;
    auto& store = descriptorStore();
    if (!store.contains(rootKey) && store.size() >= 8) {
        auto oldest = store.begin();
        for (auto it = store.begin(); it != store.end(); ++it) {
            if (it->second.second < oldest->second.second) oldest = it;
        }
        store.erase(oldest);
    }
    store[rootKey] = {std::move(data), ++useCounter()};
}

// fully composed frames of static scenes, keyed like the posters.
std::vector<std::pair<std::string, Ref<CCTexture2D>>>& bakedStore() {
    static auto* store = new std::vector<std::pair<std::string, Ref<CCTexture2D>>>();
    return *store;
}

CCTexture2D* bakedGet(std::string const& key) {
    auto& store = bakedStore();
    for (auto it = store.begin(); it != store.end(); ++it) {
        if (it->first != key) continue;
        auto entry = std::move(*it);
        store.erase(it);
        store.push_back(std::move(entry));
        return store.back().second.data();
    }
    return nullptr;
}

void bakedPut(std::string const& key, CCTexture2D* texture) {
    if (key.empty() || !texture) return;
    auto& store = bakedStore();
    std::erase_if(store, [&](auto const& entry) { return entry.first == key; });
    store.emplace_back(key, Ref<CCTexture2D>(texture));
    while (store.size() > 3) store.erase(store.begin());
}

std::unordered_set<SceneWallpaperNode*>& liveSceneNodes() {
    static auto* nodes = new std::unordered_set<SceneWallpaperNode*>();
    return *nodes;
}

// bumps on every gl context reload; buffers created before it are dead names.
uint64_t& glEpoch() { static uint64_t epoch = 1; return epoch; }

Ref<CCTexture2D> createRgbaTexture(uint8_t const* rgba, int width, int height, bool nearest) {
    if (!rgba || width < 1 || height < 1) return nullptr;
    auto texture = Ref<CCTexture2D>::adopt(new (std::nothrow) CCTexture2D);
    if (!texture || !texture->initWithData(rgba, kCCTexture2DPixelFormat_RGBA8888,
        width, height, {static_cast<float>(width), static_cast<float>(height)})) return nullptr;
    GLenum const filter = nearest ? GL_NEAREST : GL_LINEAR;
    ccTexParams params{filter, filter, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);
    return texture;
}

bool s3tcSupported() {
    static int cached = -1;
    if (cached < 0) {
        auto* config = CCConfiguration::sharedConfiguration();
        cached = config && config->checkForGLExtension("GL_EXT_texture_compression_s3tc") ? 1 : 0;
    }
    return cached == 1;
}

int maxTextureDimension() {
    auto* config = CCConfiguration::sharedConfiguration();
    int const value = config ? config->getMaxTextureSize() : 0;
    return value > 0 ? std::min(value, 16384) : 4096;
}

// dxt levels upload as-is: no cpu decode and 4-8x less vram than rgba.
Ref<CCTexture2D> uploadTexture(SceneTexture const& t, size_t& bytes) {
    bytes = 0;
    if (t.format == SceneTextureFormat::RGBA8) {
        if (t.pixels.size() < static_cast<size_t>(t.width) * t.height * 4) return nullptr;
        bytes = static_cast<size_t>(t.width) * t.height * 4;
        return createRgbaTexture(t.pixels.data(), t.width, t.height, t.nearest);
    }
    auto texture = Ref<CCTexture2D>::adopt(new (std::nothrow) CCTexture2D);
    if (!texture || !texture->initWithData(nullptr, kCCTexture2DPixelFormat_RGBA8888,
        t.width, t.height, {static_cast<float>(t.width), static_cast<float>(t.height)})) return nullptr;
    GLenum const format = t.format == SceneTextureFormat::DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
        : t.format == SceneTextureFormat::DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    ccGLBindTexture2D(texture->getName());
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, t.width, t.height, 0,
        static_cast<GLsizei>(t.pixels.size()), t.pixels.data());
    if (glGetError() == GL_NO_ERROR) {
        bytes = t.pixels.size();
    } else {
        // driver refused the blocks: decode on the cpu once.
        int const fmt = t.format == SceneTextureFormat::DXT1 ? 7 : t.format == SceneTextureFormat::DXT3 ? 6 : 4;
        std::vector<uint8_t> rgba;
        if (!scene_detail::decodePixels(fmt, t.width, t.height, t.pixels, rgba)) return nullptr;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        bytes = rgba.size();
    }
    GLenum const filter = t.nearest ? GL_NEAREST : GL_LINEAR;
    ccTexParams params{filter, filter, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);
    return texture;
}

Ref<CCTexture2D>& whiteTextureSlot() { static auto* slot = new Ref<CCTexture2D>(); return *slot; }
Ref<CCTexture2D>& particleTextureSlot() { static auto* slot = new Ref<CCTexture2D>(); return *slot; }
Ref<CCTexture2D>& dummyTextureSlot() { static auto* slot = new Ref<CCTexture2D>(); return *slot; }

CCTexture2D* whiteTexture() {
    auto& slot = whiteTextureSlot();
    if (!slot) {
        uint8_t const pixels[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
        slot = createRgbaTexture(pixels, 2, 2, false);
    }
    return slot.data();
}

// soft round sprite for particle systems whose texture ships with WE itself.
CCTexture2D* defaultParticleTexture() {
    auto& slot = particleTextureSlot();
    if (slot) return slot.data();
    constexpr int S = 32;
    std::vector<uint8_t> pixels(S * S * 4);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float const dx = (x + 0.5f - S / 2.f) / (S / 2.f);
            float const dy = (y + 0.5f - S / 2.f) / (S / 2.f);
            float const d = std::clamp(1.f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
            size_t const idx = static_cast<size_t>(y * S + x) * 4;
            pixels[idx] = pixels[idx + 1] = pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<uint8_t>(d * d * 255.f);
        }
    }
    slot = createRgbaTexture(pixels.data(), S, S, false);
    return slot.data();
}

CCTexture2D* reloadDummyTexture() {
    auto& slot = dummyTextureSlot();
    slot = nullptr; // released while the old context is still current
    uint8_t const pixel[4] = {0, 0, 0, 0};
    slot = createRgbaTexture(pixel, 1, 1, false);
    return slot.data();
}

CCRect frameRect(SceneTextureFrame const& frame, float sheetWidth, float sheetHeight) {
    float fw = frame.width != 0.f ? frame.width : frame.heightX;
    float fh = frame.height != 0.f ? frame.height : frame.widthY;
    float fx = frame.x, fy = frame.y;
    bool const uv = std::abs(fw) <= 1.0001f && std::abs(fh) <= 1.0001f &&
        std::abs(fx) <= 1.0001f && std::abs(fy) <= 1.0001f && sheetWidth > 1.f && sheetHeight > 1.f;
    if (uv) { fx *= sheetWidth; fy *= sheetHeight; fw *= sheetWidth; fh *= sheetHeight; }
    float const w = std::abs(fw), h = std::abs(fh);
    float const x = std::min(fx, fx + fw), y = std::min(fy, fy + fh);
    if (w < 0.5f || h < 0.5f || x < -0.5f || y < -0.5f || x + w > sheetWidth + 0.5f || y + h > sheetHeight + 0.5f) {
        return {0.f, 0.f, sheetWidth, sheetHeight};
    }
    return {std::max(0.f, x), std::max(0.f, y), w, h};
}

uint8_t byteOf(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }

// ---------------------------------------------------------------------------
// posters: the last composed frame, painted before anything decodes.

std::filesystem::path posterFolder() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "scene-posters";
}

std::string posterKeyFor(std::filesystem::path const& rootDir, CCSize viewport, std::string const& fitMode) {
    std::error_code ec;
    auto const package = rootDir / "scene.pkg";
    auto const described = std::filesystem::is_regular_file(package, ec) ? package : rootDir / "scene.json";
    auto const stamp = std::filesystem::last_write_time(described, ec);
    auto const stampValue = ec ? std::int64_t{-1}
        : std::chrono::duration_cast<std::chrono::seconds>(stamp.time_since_epoch()).count();
    // v2: posters rendered by the 0-based layout; v1 files only held the clear colour.
    auto const text = fmt::format("v2|{}|{}x{}|{}|{}", paimon::paths::toUtf8(rootDir),
        static_cast<int>(std::lround(viewport.width)), static_cast<int>(std::lround(viewport.height)),
        fitMode, stampValue);
    return fmt::format("{:016x}", paimon::hash::fnv1a64(text));
}

std::filesystem::path posterPath(std::string const& key) {
    return posterFolder() / (key + ".png");
}

void savePosterAsync(std::string key, std::vector<uint8_t> rgba, int width, int height) {
    if (key.empty() || rgba.empty() || width < 1 || height < 1) return;
    paimon::ThreadTracker::get().spawn([key = std::move(key), rgba = std::move(rgba), width, height]() mutable {
        geode::utils::thread::setName("ScenePosterSave");
        if (paimon::isRuntimeShuttingDown()) return;
        std::error_code ec;
        std::filesystem::create_directories(posterFolder(), ec);
        std::vector<uint8_t> png;
        if (!ImageConverter::rgbaToPngBuffer(rgba.data(), static_cast<uint32_t>(width),
            static_cast<uint32_t>(height), png)) return;
        paimon::file::writeAtomically(posterPath(key), std::span<uint8_t const>(png.data(), png.size()));

        constexpr size_t kMaxPosters = 24;
        std::vector<std::filesystem::directory_entry> files;
        for (auto const& entry : std::filesystem::directory_iterator(posterFolder(), ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".png") files.push_back(entry);
        }
        if (files.size() <= kMaxPosters) return;
        std::sort(files.begin(), files.end(), [](auto const& lhs, auto const& rhs) {
            std::error_code a, b;
            return std::filesystem::last_write_time(lhs.path(), a) < std::filesystem::last_write_time(rhs.path(), b);
        });
        for (size_t i = 0; i + kMaxPosters < files.size(); ++i) std::filesystem::remove(files[i].path(), ec);
    });
}

void applyNodeTransform(CCNode* node, SceneAffine const& m) {
    auto const t = decomposeSceneAffine(m);
    node->setPosition({t.x, t.y});
    node->setScaleX(t.scaleX);
    node->setScaleY(t.scaleY);
    node->setRotationX(t.rotationX);
    node->setRotationY(t.rotationY);
}

std::string formatClock(bool use24, bool seconds, std::string const& delimiter) {
    std::time_t const now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    int hour = local.tm_hour;
    if (!use24) {
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    auto text = fmt::format("{:02}{}{:02}", hour, delimiter, local.tm_min);
    if (seconds) text += fmt::format("{}{:02}", delimiter, local.tm_sec);
    return text;
}

// order holds indices into {year, month, day}, as the date script wrote them.
std::string formatDate(std::array<uint8_t, 3> const& order, bool year, std::string const& delimiter) {
    std::time_t const now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::string const parts[3] = {fmt::format("{}", local.tm_year + 1900), fmt::format("{:02}", local.tm_mon + 1),
        fmt::format("{:02}", local.tm_mday)};
    std::string text;
    for (auto index : order) {
        if (index > 2 || (index == 0 && !year)) continue;
        if (!text.empty()) text += delimiter;
        text += parts[index];
    }
    return text;
}

std::string liveText(SceneTextClock kind, bool use24, bool seconds, std::string const& delimiter,
    std::array<uint8_t, 3> const& order, bool year) {
    return kind == SceneTextClock::Date ? formatDate(order, year, delimiter) : formatClock(use24, seconds, delimiter);
}

} // namespace

// ---------------------------------------------------------------------------
// particle layer: simulation at <= 60 Hz, one batched draw per system.

class SceneParticleNode : public CCNode {
public:
    static SceneParticleNode* create(std::shared_ptr<SceneParticleDesc const> desc, CCTexture2D* texture,
        std::vector<std::array<float, 4>> frames, float frameDuration, ccBlendFunc blend, bool premultiplied, uint32_t seed) {
        auto* node = new (std::nothrow) SceneParticleNode;
        if (node && node->init(std::move(desc), texture, std::move(frames), frameDuration, blend, premultiplied, seed)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    ~SceneParticleNode() override {
        if (m_vbo && m_vboEpoch == glEpoch()) glDeleteBuffers(1, &m_vbo);
    }

    void step(float dt) {
        m_accumulated += dt;
        constexpr float kInterval = 1.f / 60.f;
        if (m_accumulated < kInterval) return;
        float const elapsed = std::min(m_accumulated, 0.1f);
        m_accumulated = 0.f;
        m_sim->step(elapsed);
        rebuild();
    }

    void setTexture(CCTexture2D* texture) { m_texture = texture; }

    void draw() override {
        if (m_vertexCount == 0 || !m_texture) return;
        CC_NODE_DRAW_SETUP();
        ccGLBlendFunc(m_blend.src, m_blend.dst);
        ccGLBindTexture2D(m_texture->getName());
        if (m_vboEpoch != glEpoch()) {
            m_vbo = 0; // name died with the old context
            m_vboEpoch = glEpoch();
            m_uploaded = false;
        }
        if (!m_vbo) glGenBuffers(1, &m_vbo);
#if CC_TEXTURE_ATLAS_USE_VAO
        ccGLBindVAO(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        if (!m_uploaded) {
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertexCount * sizeof(ccV3F_C4B_T2F)),
                m_vertices.data(), GL_STREAM_DRAW);
            m_uploaded = true;
        }
        ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
        constexpr GLsizei stride = sizeof(ccV3F_C4B_T2F);
        glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<void*>(offsetof(ccV3F_C4B_T2F, vertices)));
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
            reinterpret_cast<void*>(offsetof(ccV3F_C4B_T2F, colors)));
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<void*>(offsetof(ccV3F_C4B_T2F, texCoords)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertexCount));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        CC_INCREMENT_GL_DRAWS(1);
    }

private:
    bool init(std::shared_ptr<SceneParticleDesc const> desc, CCTexture2D* texture,
        std::vector<std::array<float, 4>> frames, float frameDuration, ccBlendFunc blend, bool premultiplied, uint32_t seed) {
        if (!CCNode::init() || !desc) return false;
        m_sim.emplace(*desc, seed);
        m_texture = texture;
        m_frames = std::move(frames);
        m_frameDuration = std::max(frameDuration, 0.01f);
        m_blend = blend;
        m_premultiplied = premultiplied;
        // wallpaper engine draws a particle `size` wide and size * (frame
        // height / width) tall: light shafts and rain streaks are not squares.
        float const texW = texture ? static_cast<float>(texture->getPixelsWide()) : 1.f;
        float const texH = texture ? static_cast<float>(texture->getPixelsHigh()) : 1.f;
        m_ratios.reserve(m_frames.size());
        for (auto const& uv : m_frames) {
            float const w = std::abs(uv[2] - uv[0]) * texW;
            float const h = std::abs(uv[3] - uv[1]) * texH;
            m_ratios.push_back(w > 0.f && h > 0.f ? std::clamp(h / w, 0.05f, 20.f) : 1.f);
        }
        setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
        setAnchorPoint({0.f, 0.f});
        m_sim->prewarm();
        rebuild();
        return true;
    }

    void rebuild() {
        m_vertices.clear();
        auto const& desc = m_sim->desc();
        std::array<float, 4> const full{0.f, 0.f, 1.f, 1.f};
        m_sim->forEachSprite([&](SceneParticleSim::Sprite const& s) {
            size_t frame = 0;
            if (m_frames.size() > 1) {
                float const count = static_cast<float>(m_frames.size());
                float const position = desc.randomFrame ? s.seed * count : s.life * count * desc.sequenceMultiplier;
                frame = static_cast<size_t>(std::max(0.f, position)) % m_frames.size();
            }
            auto const& uv = m_frames.empty() ? full : m_frames[frame];
            float const ratio = m_ratios.empty() ? 1.f : m_ratios[frame];
            float const half = s.size * 0.5f;
            float ux = 1.f, uy = 0.f, vx = 0.f, vy = 1.f;
            float hx = half, hy = half * ratio;
            float const speed = std::hypot(s.vx, s.vy);
            if (desc.trail && speed > 1e-3f) {
                ux = s.vx / speed; uy = s.vy / speed;
                vx = -uy; vy = ux;
                hx = std::max(half, speed * desc.trailLength * 0.5f);
                hy = half;
            } else if (s.rotation != 0.f) {
                // genericparticle rotates clockwise: right = (cos, -sin).
                float const c = std::cos(s.rotation), n = std::sin(s.rotation);
                ux = c; uy = -n; vx = n; vy = c;
            }
            float const tint = m_premultiplied ? s.alpha : 1.f;
            ccColor4B const color{byteOf(s.r * tint), byteOf(s.g * tint), byteOf(s.b * tint), byteOf(s.alpha)};
            auto corner = [&](float sx, float sy, float u, float v) {
                ccV3F_C4B_T2F vertex;
                vertex.vertices = {s.x + ux * hx * sx + vx * hy * sy, s.y + uy * hx * sx + vy * hy * sy, 0.f};
                vertex.colors = color;
                vertex.texCoords = {u, v};
                m_vertices.push_back(vertex);
            };
            // texture rows run top-down: the top edge samples v0.
            corner(-1.f, -1.f, uv[0], uv[3]);
            corner(1.f, -1.f, uv[2], uv[3]);
            corner(-1.f, 1.f, uv[0], uv[1]);
            corner(-1.f, 1.f, uv[0], uv[1]);
            corner(1.f, -1.f, uv[2], uv[3]);
            corner(1.f, 1.f, uv[2], uv[1]);
        });
        m_vertexCount = m_vertices.size();
        m_uploaded = false;
    }

    std::optional<SceneParticleSim> m_sim;
    Ref<CCTexture2D> m_texture;
    std::vector<std::array<float, 4>> m_frames;
    std::vector<float> m_ratios;
    float m_frameDuration = 0.033f;
    ccBlendFunc m_blend = kNormalBlend;
    bool m_premultiplied = false;
    std::vector<ccV3F_C4B_T2F> m_vertices;
    size_t m_vertexCount = 0;
    bool m_uploaded = false;
    float m_accumulated = 0.f;
    GLuint m_vbo = 0;
    uint64_t m_vboEpoch = 0;
};

// ---------------------------------------------------------------------------

SceneWallpaperNode* SceneWallpaperNode::create(std::filesystem::path const& project,
    CCSize viewport, LayerBgConfig const& config) {
    auto* node = new (std::nothrow) SceneWallpaperNode;
    if (node && node->initScene(project, viewport, config)) {
        node->autorelease();
        return node;
    }
    CC_SAFE_DELETE(node);
    return nullptr;
}

SceneWallpaperNode::~SceneWallpaperNode() {
    if (m_cancelFlag) m_cancelFlag->store(true, std::memory_order_release);
    liveSceneNodes().erase(this);
}

void SceneWallpaperNode::onEnter() {
    CCNode::onEnter();
    if (!m_animated.empty() || !m_particles.empty() || !m_videos.empty() || m_parallax) scheduleUpdate();
}

void SceneWallpaperNode::onExit() {
    unscheduleUpdate();
    pauseVideos();
    CCNode::onExit();
}

float SceneWallpaperNode::pixelsPerPoint() const {
    auto* director = CCDirector::get();
    auto* view = director ? director->getOpenGLView() : nullptr;
    auto const win = director ? director->getWinSize() : CCSize{};
    if (!view || win.width <= 0.f) return 1.f;
    float const frame = view->getFrameSize().width;
    return frame > 0.f ? frame / win.width : 1.f;
}

void SceneWallpaperNode::showStatus(std::string const& message, bool error) {
    auto const viewport = getContentSize();
    if (!m_status) {
        m_status = CCLabelBMFont::create(message.c_str(), "chatFont.fnt");
        if (!m_status) return;
        m_status->setAlignment(kCCTextAlignmentCenter);
        m_status->setScale(std::min(.6f, viewport.width / 500.f));
        m_status->setPosition(viewport / 2.f);
        addChild(m_status, 10);
    } else {
        m_status->setString(message.c_str());
    }
    m_status->setColor(error ? ccColor3B{255, 110, 110} : ccColor3B{255, 255, 255});
    m_status->setVisible(true);
}

void SceneWallpaperNode::clearLayers() {
    if (m_world) {
        m_world->removeFromParentAndCleanup(true);
        m_world = nullptr;
    }
    if (m_baked) {
        m_baked->removeFromParentAndCleanup(true);
        m_baked = nullptr;
    }
    m_drawables.clear();
    m_roots.clear();
    m_animated.clear();
    m_videos.clear();
    m_particles.clear();
    m_clocks.clear();
    m_textureRefs.clear();
    // late video opens of the previous build must not attach to new layers.
    ++m_buildGeneration;
    if (m_clockScheduled) {
        unschedule(schedule_selector(SceneWallpaperNode::tickClocks));
        m_clockScheduled = false;
    }
}

void SceneWallpaperNode::invalidateTextures(CCTexture2D* dummy) {
    // runs while the old context is alive: textures referenced here are
    // released on the right context, sprites keep a valid 1x1 meanwhile.
    for (auto& d : m_drawables) {
        if (d.sprite && dummy) d.sprite->setTexture(dummy);
        // players own gl textures: release them on the old context.
        if (d.video) {
            d.video.reset();
            d.videoStarted = d.videoPaused = d.videoShown = false;
            if (d.sprite) d.sprite->setVisible(false);
        }
    }
    for (auto* particles : m_particles) particles->setTexture(dummy);
    m_textureRefs.clear();
    if (m_poster) {
        m_poster->removeFromParentAndCleanup(true);
        m_poster = nullptr;
    }
    if (m_baked || !m_pendingUploads.empty() || !m_drawables.empty()) m_rebuildPending = true;
    if (m_baked) {
        m_baked->removeFromParentAndCleanup(true);
        m_baked = nullptr;
    }
    m_pendingUploads.clear();
    m_pendingData.reset();
    m_uploadIndex = 0;
}

void SceneWallpaperNode::onGLContextReload() {
    ++glEpoch();
    textureStore().clear();
    textureStoreBytes() = 0;
    bakedStore().clear();
    whiteTextureSlot() = nullptr;
    particleTextureSlot() = nullptr;
    auto* dummy = reloadDummyTexture();
    std::vector<SceneWallpaperNode*> nodes(liveSceneNodes().begin(), liveSceneNodes().end());
    for (auto* node : nodes) node->invalidateTextures(dummy);
    for (auto* node : nodes) node->restartAfterContextLoss();
}

void SceneWallpaperNode::restartAfterContextLoss() {
    if (!m_rebuildPending) return;
    m_rebuildPending = false;
    m_retriedMissing = false;
    clearLayers();
    if (m_uploadScheduled) {
        unschedule(schedule_selector(SceneWallpaperNode::uploadStep));
        m_uploadScheduled = false;
    }
    showStatus("Cargando escena...", false);
    // descriptors are cpu-only and survived the reload: only uploads repeat.
    startAsyncLoad(descriptorGet(m_rootKey));
}

bool SceneWallpaperNode::visibleInHierarchy() const {
    return isNodeVisibleInHierarchy(this);
}

void SceneWallpaperNode::showPoster(Ref<CCTexture2D> texture) {
    if (!texture || m_world || m_baked || m_poster) return;
    auto* sprite = CCSprite::createWithTexture(texture.data());
    if (!sprite) return;
    auto const viewport = getContentSize();
    auto const size = sprite->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;
    // posters are captures of this exact viewport.
    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});
    sprite->setScaleX(viewport.width / size.width);
    sprite->setScaleY(viewport.height / size.height);
    addChild(sprite, -1);
    m_poster = sprite;
    if (m_status) m_status->setVisible(false);
}

// decodes the poster png off the main thread; only the upload happens here.
void SceneWallpaperNode::requestPoster() {
    if (m_posterKey.empty() || m_posterShown) return;
    m_posterShown = true;
    WeakRef<SceneWallpaperNode> weak = this;
    auto const path = posterPath(m_posterKey);
    paimon::ThreadTracker::get().spawn([weak, path]() {
        geode::utils::thread::setName("ScenePosterLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        auto bytes = utils::file::readBinary(paimon::paths::toSdkPath(path)).unwrapOr(std::vector<uint8_t>{});
        if (bytes.empty()) return;
        int w = 0, h = 0, comp = 0;
        auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
        if (!pixels) return;
        auto rgba = std::make_shared<std::vector<uint8_t>>(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
        geode::Loader::get()->queueInMainThread([weak, rgba, w, h]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto node = weak.lock();
            if (!node) return;
            node->showPoster(createRgbaTexture(rgba->data(), w, h, false));
        });
    });
}

Ref<CCRenderTexture> SceneWallpaperNode::renderSurface(bool opaque) {
    auto const viewport = getContentSize();
    if (viewport.width < 1.f || viewport.height < 1.f ||
        viewport.width > 4096.f || viewport.height > 4096.f) return nullptr;
    Ref<CCRenderTexture> surface = CCRenderTexture::create(
        static_cast<int>(viewport.width), static_cast<int>(viewport.height));
    if (!surface) return nullptr;
    bool const statusVisible = m_status && m_status->isVisible();
    bool const posterVisible = m_poster && m_poster->isVisible();
    if (m_status) m_status->setVisible(false);
    if (m_poster) m_poster->setVisible(false);
    surface->beginWithClear(0.f, 0.f, 0.f, opaque ? 1.f : 0.f);
    visit();
    surface->end();
    if (m_status) m_status->setVisible(statusVisible);
    if (m_poster) m_poster->setVisible(posterVisible);
    return surface;
}

void SceneWallpaperNode::queuePosterSave(Ref<CCRenderTexture> surface) {
    if (!surface || m_posterKey.empty() || m_posterSaved) return;
    m_posterSaved = true;
    std::error_code ec;
    if (std::filesystem::is_regular_file(posterPath(m_posterKey), ec)) return;
    auto* image = surface->newCCImage(true);
    if (!image) return;
    int const width = static_cast<int>(image->getWidth());
    int const height = static_cast<int>(image->getHeight());
    auto* data = image->getData();
    if (width < 1 || height < 1 || !data) { image->release(); return; }
    std::vector<uint8_t> pixels(data, data + static_cast<size_t>(width) * height * 4);
    image->release();
    // a scene is opaque; transparent texels would let the menu bleed through.
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0xFF;
    savePosterAsync(m_posterKey, std::move(pixels), width, height);
}

bool SceneWallpaperNode::initScene(std::filesystem::path const& project,
    CCSize viewport, LayerBgConfig const& config) {
    if (!CCNode::init() || viewport.width <= 0.f || viewport.height <= 0.f) return false;

    std::filesystem::path rootDir = project;
    std::error_code ec;
    if (std::filesystem::is_regular_file(rootDir, ec)) rootDir = rootDir.parent_path();

    m_root = rootDir;
    m_rootKey = paimon::paths::toUtf8(rootDir.lexically_normal());
    m_fitMode = config.fitMode;
    m_parallax = config.parallax && config.parallaxStrength > 0.f;
    m_strength = std::clamp(config.parallaxStrength, 0.f, 2.f);
    m_posterKey = posterKeyFor(rootDir, viewport, m_fitMode + (m_parallax ? "|p" : ""));

    setContentSize(viewport);
    setAnchorPoint({0.f, 0.f});

    m_background = CCLayerColor::create({0, 0, 0, 255});
    if (m_background) {
        m_background->setContentSize(viewport);
        addChild(m_background, -2);
    }
    liveSceneNodes().insert(this);

    // a static scene composed earlier in this session: one sprite, no io.
    if (showBaked()) return true;

    // descriptors plus uploaded textures cached: rebuild with no worker.
    auto descriptors = descriptorGet(m_rootKey);
    if (descriptors) {
        SceneDecodeCaps caps;
        caps.viewportW = viewport.width;
        caps.viewportH = viewport.height;
        caps.pixelsPerPoint = pixelsPerPoint();
        caps.fitMode = m_fitMode;
        caps.overscan = m_parallax ? 1.08f : 1.f;
        float const ppu = pixelsPerSceneUnit(*descriptors, caps);
        bool ready = true;
        for (auto const& path : descriptors->textureOrder) {
            auto* entry = textureGet(textureKey(m_rootKey, path));
            auto const [tw, th] = sceneTextureTarget(*descriptors, path, ppu);
            if (!entry || !entry->coverage.satisfies(tw, th)) { ready = false; break; }
        }
        if (ready) {
            buildScene(descriptors);
            return true;
        }
    }

    showStatus("Cargando escena...", false);
    requestPoster();
    startAsyncLoad(std::move(descriptors));
    return true;
}

void SceneWallpaperNode::startAsyncLoad(std::shared_ptr<SceneData const> descriptors) {
    ++m_loadGeneration;
    if (m_cancelFlag) m_cancelFlag->store(true, std::memory_order_release);
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);

    SceneDecodeCaps caps;
    auto const viewport = getContentSize();
    caps.viewportW = viewport.width;
    caps.viewportH = viewport.height;
    caps.pixelsPerPoint = pixelsPerPoint();
    caps.fitMode = m_fitMode;
    caps.overscan = m_parallax ? 1.08f : 1.f;
    caps.keepCompressed = s3tcSupported();
    caps.maxDimension = maxTextureDimension();
    caps.videoCacheDir = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "scene-videos";

    auto job = [root = m_root, generation = m_loadGeneration, cancel = m_cancelFlag,
                cached = coverageSnapshot(m_rootKey), caps, descriptors = std::move(descriptors),
                weak = WeakRef<SceneWallpaperNode>(this)]() mutable {
        geode::utils::thread::setName("SceneLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        std::shared_ptr<SceneData const> data = std::move(descriptors);
        std::vector<std::pair<std::string, SceneTexture>> decoded;
        bool extracted = false;
        auto package = ScenePackage::open(root);
        if (package) {
            package->setFallbackRoot(WallpaperEngineManager::get().wallpaperEngineAssetsDir());
            if (!data) {
                data = extractSceneData(*package);
                extracted = data != nullptr;
            }
            auto const cancelled = [&cancel] {
                    return cancel->load(std::memory_order_relaxed) || paimon::isRuntimeShuttingDown();
                };
                if (data && !data->perspective && !decodeSceneTextures(*package, *data, cached, caps, cancelled, decoded)) return;
        }
        bool const failed = !data;
        bool const missingPackage = !package;
        geode::Loader::get()->queueInMainThread(
            [weak, generation, data, decoded = std::move(decoded), extracted, failed, missingPackage]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                auto node = weak.lock();
                if (!node || node->m_loadGeneration != generation) return;
                if (failed) {
                    node->showStatus(missingPackage ? "No se pudo abrir el paquete de la escena."
                        : "No se pudo leer la escena.", true);
                    return;
                }
                node->queueUploads(data, std::move(decoded), extracted);
            });
    };
    if (!paimon::ThreadTracker::get().spawn(std::move(job))) {
        showStatus("La escena no puede cargar durante el cierre.", true);
    }
}

void SceneWallpaperNode::queueUploads(std::shared_ptr<SceneData const> data,
    std::vector<std::pair<std::string, SceneTexture>>&& decoded, bool extracted) {
    if (extracted) descriptorPut(m_rootKey, data);
    m_pendingData = std::move(data);
    m_pendingUploads = std::move(decoded);
    m_uploadIndex = 0;
    uploadStep(0.f);
    if (m_pendingData && !m_uploadScheduled) {
        m_uploadScheduled = true;
        schedule(schedule_selector(SceneWallpaperNode::uploadStep));
    }
}

// uploads a few textures per frame so a big scene never stalls the game;
// the poster covers the viewport meanwhile.
void SceneWallpaperNode::uploadStep(float) {
    if (!m_pendingData) return;
    auto const started = std::chrono::steady_clock::now();
    constexpr auto kBudget = std::chrono::milliseconds(5);
    bool first = true;
    while (m_uploadIndex < m_pendingUploads.size()) {
        if (!first && std::chrono::steady_clock::now() - started > kBudget) return;
        first = false;
        auto& [path, texture] = m_pendingUploads[m_uploadIndex++];
        TextureEntry entry;
        if (texture.video) {
            if (texture.videoFile.empty()) continue;
            entry.videoFile = texture.videoFile;
        } else {
            size_t bytes = 0;
            auto uploaded = uploadTexture(texture, bytes);
            if (!uploaded) continue;
            entry.texture = std::move(uploaded);
            entry.bytes = bytes;
        }
        entry.frames = std::move(texture.frames);
        entry.visibleW = texture.visibleWidth;
        entry.visibleH = texture.visibleHeight;
        entry.sourceW = texture.sourceWidth > 0 ? texture.sourceWidth : texture.visibleWidth;
        entry.sourceH = texture.sourceHeight > 0 ? texture.sourceHeight : texture.visibleHeight;
        entry.coverage = coverageOf(texture);
        texture.pixels = {};
        texturePut(textureKey(m_rootKey, path), std::move(entry));
    }
    if (m_uploadScheduled) {
        unschedule(schedule_selector(SceneWallpaperNode::uploadStep));
        m_uploadScheduled = false;
    }
    m_pendingUploads.clear();
    m_uploadIndex = 0;
    auto data = std::move(m_pendingData);
    m_pendingData.reset();
    buildScene(data);
}

void SceneWallpaperNode::applyTransform(Drawable& d) {
    auto m = sceneScaled(d.world, d.fitX, d.fitY);
    m.tx += d.offsetX;
    m.ty += d.offsetY;
    applyNodeTransform(d.node, m);
}

void SceneWallpaperNode::buildScene(std::shared_ptr<SceneData const> data) {
    if (!data) {
        showStatus("No se pudo leer la escena.", true);
        return;
    }
    if (data->perspective) {
        // drawing 3D models flat produced a meaningless collage.
        showStatus("Esta escena es 3D.\nEl motor 2D no puede mostrarla.", true);
        return;
    }
    // a texture evicted (LRU, or baked away by another node showing this
    // scene) between the decode snapshot and now would silently drop a layer.
    if (!m_retriedMissing) {
        bool missing = false;
        for (auto const& o : data->objects) {
            if (!o.shown || o.texturePath.empty() ||
                (o.kind != SceneObjectKind::Sprite && o.kind != SceneObjectKind::Particle)) continue;
            if (!textureGet(textureKey(m_rootKey, o.texturePath))) { missing = true; break; }
        }
        if (missing) {
            m_retriedMissing = true;
            startAsyncLoad(std::move(data));
            return;
        }
    }
    clearLayers();

    auto const viewport = getContentSize();
    if (m_background) {
        m_background->setColor({byteOf(data->clear[0]), byteOf(data->clear[1]), byteOf(data->clear[2])});
    }

    // layers only move when the author enabled camera parallax; most scenes
    // leave every parallaxDepth at 1 with it off, and moving them all slid
    // and zoomed the whole wallpaper.
    float maxDepth = 0.f;
    if (m_parallax && data->cameraParallax) {
        for (auto const& o : data->objects) {
            if (o.root >= 0 && static_cast<size_t>(o.root) < data->objects.size() && o.shown) {
                auto const& root = data->objects[static_cast<size_t>(o.root)];
                maxDepth = std::max({maxDepth, std::abs(root.depth[0]), std::abs(root.depth[1])});
            }
        }
    }
    bool const parallax = m_parallax && data->cameraParallax && maxDepth > 0.f;
    // cameraparallaxamount defaults to 0.5, which the user strength scales.
    m_parallaxScale = data->parallaxAmount * 2.f;
    m_parallaxRate = 1.f / std::max(data->parallaxDelay, 1.f / 30.f);
    auto layout = fitWallpaper(data->sceneW, data->sceneH, viewport.width, viewport.height, m_fitMode);
    if (parallax) {
        float const reach = parallaxOffset(std::min(maxDepth, 2.f), 1.f, m_strength * m_parallaxScale);
        float const k = std::min(1.25f, 1.f + 2.f * reach / std::max(1.f, std::min(viewport.width, viewport.height)));
        float const cx = layout.left + layout.width * 0.5f, cy = layout.bottom + layout.height * 0.5f;
        layout.width *= k;
        layout.height *= k;
        layout.left = cx - layout.width * 0.5f;
        layout.bottom = cy - layout.height * 0.5f;
    }
    float const worldScaleX = layout.width / std::max(data->sceneW, 1.f);
    float const worldScaleY = layout.height / std::max(data->sceneH, 1.f);

    m_world = CCNode::create();
    m_world->setAnchorPoint({0.f, 0.f});
    m_world->setPosition({layout.left, layout.bottom});
    m_world->setScaleX(worldScaleX);
    m_world->setScaleY(worldScaleY);
    addChild(m_world, 0);

    float const csf = std::max(CCDirector::get()->getContentScaleFactor(), 0.01f);

    m_roots.assign(data->objects.size(), RootDepth{});
    for (size_t i = 0; i < data->objects.size(); ++i) {
        m_roots[i] = {data->objects[i].depth[0], data->objects[i].depth[1]};
    }

    for (size_t i = 0; i < data->objects.size(); ++i) {
        auto const& o = data->objects[i];
        if (!o.shown || o.kind == SceneObjectKind::Group) continue;
        Drawable d;
        d.world = o.world;
        d.root = o.root;

        switch (o.kind) {
            case SceneObjectKind::Sprite: {
                auto* entry = textureGet(textureKey(m_rootKey, o.texturePath));
                if (!entry) break;
                if (!entry->videoFile.empty()) {
                    // shown once the player decoded its first frame.
                    auto* sprite = CCSprite::createWithTexture(whiteTexture());
                    if (!sprite) break;
                    sprite->setVisible(false);
                    d.sizeX = o.size[0] > 0.f ? o.size[0] : static_cast<float>(entry->sourceW);
                    d.sizeY = o.size[1] > 0.f ? o.size[1] : static_cast<float>(entry->sourceH);
                    auto const content = sprite->getContentSize();
                    d.fitX = content.width > 0.f ? d.sizeX / content.width : 1.f;
                    d.fitY = content.height > 0.f ? d.sizeY / content.height : 1.f;
                    d.videoFile = entry->videoFile;
                    d.sprite = sprite;
                    d.node = sprite;
                } else {
                    if (!entry->texture) break;
                    auto* sprite = CCSprite::createWithTexture(entry->texture.data());
                    if (!sprite) break;
                    float const texW = static_cast<float>(entry->texture->getPixelsWide());
                    float const texH = static_cast<float>(entry->texture->getPixelsHigh());
                    auto toPoints = [csf](CCRect r) {
                        return CCRect{r.origin.x / csf, r.origin.y / csf, r.size.width / csf, r.size.height / csf};
                    };
                    CCRect const firstPx = entry->frames.empty()
                        ? CCRect{0.f, 0.f, static_cast<float>(entry->visibleW), static_cast<float>(entry->visibleH)}
                        : frameRect(entry->frames.front(), texW, texH);
                    sprite->setTextureRect(toPoints(firstPx));
                    if (entry->frames.size() >= 2) {
                        d.animation.reserve(entry->frames.size());
                        for (auto const& f : entry->frames) {
                            d.animation.push_back({toPoints(frameRect(f, texW, texH)), f.duration < 0.01f ? 0.033f : f.duration});
                        }
                    }
                    // autosized layers keep the image's own size (in scene units).
                    float sizeX = o.size[0], sizeY = o.size[1];
                    if (sizeX <= 0.f || sizeY <= 0.f) {
                        float const cellScaleX = entry->visibleW > 0 ? static_cast<float>(entry->sourceW) / entry->visibleW : 1.f;
                        float const cellScaleY = entry->visibleH > 0 ? static_cast<float>(entry->sourceH) / entry->visibleH : 1.f;
                        sizeX = firstPx.size.width * cellScaleX;
                        sizeY = firstPx.size.height * cellScaleY;
                    }
                    auto const content = sprite->getContentSize();
                    if (content.width <= 0.f || content.height <= 0.f) break;
                    d.sizeX = sizeX;
                    d.sizeY = sizeY;
                    d.fitX = sizeX / content.width;
                    d.fitY = sizeY / content.height;
                    d.sprite = sprite;
                    d.node = sprite;
                    m_textureRefs.push_back(entry->texture);
                }
                // premultiplied composition carries the layer alpha in rgb too.
                float const tint = premultipliedBlend(o.blend) ? o.alpha : 1.f;
                d.sprite->setAnchorPoint({o.anchor.x, o.anchor.y});
                d.sprite->setColor({byteOf(o.color[0] * tint), byteOf(o.color[1] * tint), byteOf(o.color[2] * tint)});
                d.sprite->setOpacity(byteOf(o.alpha));
                d.sprite->setBlendFunc(blendFuncOf(o.blend));
                break;
            }
            case SceneObjectKind::Solid: {
                auto* white = whiteTexture();
                if (!white) break;
                auto* sprite = CCSprite::createWithTexture(white);
                if (!sprite) break;
                float const sizeX = o.size[0] > 0.f ? o.size[0] : data->sceneW;
                float const sizeY = o.size[1] > 0.f ? o.size[1] : data->sceneH;
                auto const content = sprite->getContentSize();
                if (content.width <= 0.f || content.height <= 0.f) break;
                d.fitX = sizeX / content.width;
                d.fitY = sizeY / content.height;
                float const tint = premultipliedBlend(o.blend) ? o.alpha : 1.f;
                sprite->setAnchorPoint({o.anchor.x, o.anchor.y});
                sprite->setColor({byteOf(o.color[0] * tint), byteOf(o.color[1] * tint), byteOf(o.color[2] * tint)});
                sprite->setOpacity(byteOf(o.alpha));
                sprite->setBlendFunc(blendFuncOf(o.blend));
                d.sprite = sprite;
                d.node = sprite;
                break;
            }
            case SceneObjectKind::Text: {
                bool const live = o.clock != SceneTextClock::None;
                std::string const text = live
                    ? liveText(o.clock, o.clock24, o.clockSeconds, o.clockDelimiter, o.dateOrder, o.dateYear) : o.text;
                auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
                if (!label) break;
                auto const content = label->getContentSize();
                if (content.height <= 0.f) break;
                // the authored box is the text plus padding on every side.
                float const boxW = o.size[0] - 2.f * o.padding;
                float const boxH = o.size[1] - 2.f * o.padding;
                float const lineH = boxH > 1.f ? boxH : o.pointSize * 2.9f;
                float fit = lineH / content.height;
                if (boxW > 1.f && content.width * fit > boxW * 1.15f) fit = boxW * 1.15f / content.width;
                d.fitX = d.fitY = fit;
                label->setAnchorPoint({o.anchor.x, o.anchor.y});
                label->setColor({byteOf(o.color[0]), byteOf(o.color[1]), byteOf(o.color[2])});
                label->setOpacity(byteOf(o.alpha));
                // align inside the authored box around the origin.
                float const ox = (o.anchor.x - 0.5f) * std::max(0.f, boxW);
                float const oy = (o.anchor.y - 0.5f) * std::max(0.f, boxH);
                d.world = sceneMultiply(o.world, sceneLocalTransform(ox, oy, 0.f, 1.f, 1.f));
                d.label = label;
                d.node = label;
                if (live) {
                    ClockText clock;
                    clock.drawable = m_drawables.size();
                    clock.kind = o.clock;
                    clock.use24 = o.clock24;
                    clock.seconds = o.clockSeconds;
                    clock.delimiter = o.clockDelimiter;
                    clock.dateOrder = o.dateOrder;
                    clock.dateYear = o.dateYear;
                    clock.current = text;
                    clock.boxW = boxW;
                    clock.boxH = lineH;
                    d.clock = m_clocks.size();
                    m_clocks.push_back(std::move(clock));
                }
                break;
            }
            case SceneObjectKind::Particle: {
                if (!o.particles) break;
                CCTexture2D* texture = nullptr;
                std::vector<std::array<float, 4>> frames;
                float frameDuration = 0.033f;
                if (!o.texturePath.empty()) {
                    if (auto* entry = textureGet(textureKey(m_rootKey, o.texturePath)); entry && entry->texture) {
                        texture = entry->texture.data();
                        m_textureRefs.push_back(entry->texture);
                        float const texW = static_cast<float>(entry->texture->getPixelsWide());
                        float const texH = static_cast<float>(entry->texture->getPixelsHigh());
                        if (entry->frames.size() >= 2) {
                            for (auto const& f : entry->frames) {
                                auto const r = frameRect(f, texW, texH);
                                frames.push_back({r.origin.x / texW, r.origin.y / texH,
                                    (r.origin.x + r.size.width) / texW, (r.origin.y + r.size.height) / texH});
                            }
                            frameDuration = entry->frames.front().duration;
                        } else {
                            frames.push_back({0.f, 0.f, entry->visibleW / texW, entry->visibleH / texH});
                        }
                    }
                }
                if (!texture) texture = defaultParticleTexture();
                auto* node = SceneParticleNode::create(o.particles, texture, std::move(frames),
                    frameDuration, blendFuncOf(o.blend), premultipliedBlend(o.blend), static_cast<uint32_t>(i * 2654435761u + 17u));
                if (!node) break;
                d.node = node;
                m_particles.push_back(node);
                break;
            }
            case SceneObjectKind::Group:
                break;
        }

        if (!d.node) continue;
        applyTransform(d);
        m_world->addChild(d.node, static_cast<int>(i));
        if (!d.animation.empty()) m_animated.push_back(m_drawables.size());
        if (!d.videoFile.empty()) m_videos.push_back(m_drawables.size());
        m_drawables.push_back(std::move(d));
    }

    if (!parallax) m_parallax = false;
    m_parallaxSettled = false;
    m_hasPointer = false;
    m_currentParallaxX = m_currentParallaxY = 0.f;

    if (m_poster) {
        m_poster->removeFromParentAndCleanup(true);
        m_poster = nullptr;
    }
    if (m_drawables.empty()) {
        showStatus("La escena no tiene capas compatibles.", true);
        return;
    }
    if (m_status) m_status->setVisible(false);

    if (!m_clocks.empty()) {
        bool const seconds = std::any_of(m_clocks.begin(), m_clocks.end(), [](auto const& c) { return c.seconds; });
        bool const timeOfDay = std::any_of(m_clocks.begin(), m_clocks.end(),
            [](auto const& c) { return c.kind == SceneTextClock::Time; });
        schedule(schedule_selector(SceneWallpaperNode::tickClocks), seconds ? 0.25f : timeOfDay ? 1.f : 15.f);
        m_clockScheduled = true;
    }

    if (bakeStaticScene()) return;

    if (!m_animated.empty() || !m_particles.empty() || !m_videos.empty() || m_parallax) {
        if (isRunning()) scheduleUpdate();
    } else {
        unscheduleUpdate();
    }
    startVideos();
    // cache a poster so the next visit paints before the first decode; with
    // video layers it waits for their first frame.
    if (!m_posterSaved && m_videos.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(posterPath(m_posterKey), ec)) queuePosterSave(renderSurface(true));
        else m_posterSaved = true;
    }
}

// a static scene costs one draw per layer every frame. Rendering it once into
// a viewport texture turns that into a single sprite, frees the per-layer
// textures and is reused on the next visit with no io at all.
bool SceneWallpaperNode::bakeStaticScene() {
    if (!m_animated.empty() || !m_particles.empty() || !m_clocks.empty() || !m_videos.empty() || m_parallax) return false;
    if (m_drawables.size() < 2) return false;
    float const csf = std::max(CCDirector::get()->getContentScaleFactor(), 0.01f);
    auto const viewport = getContentSize();
    // the render target is points * content scale; skip it when that would be
    // softer than the real framebuffer.
    if (viewport.width * csf < viewport.width * pixelsPerPoint() * 0.95f) return false;

    auto surface = renderSurface(true);
    auto* texture = surface && surface->getSprite() ? surface->getSprite()->getTexture() : nullptr;
    if (!texture) return false;
    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);
    queuePosterSave(surface);
    bakedPut(m_posterKey, texture);

    std::vector<std::string> keys;
    if (auto data = descriptorGet(m_rootKey)) {
        for (auto const& path : data->textureOrder) keys.push_back(textureKey(m_rootKey, path));
    }
    clearLayers();
    for (auto const& key : keys) textureErase(key);
    unscheduleUpdate();
    return showBaked();
}

bool SceneWallpaperNode::showBaked() {
    auto* texture = bakedGet(m_posterKey);
    if (!texture) return false;
    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return false;
    auto const viewport = getContentSize();
    auto const size = sprite->getContentSize();
    // render textures read back y-inverted.
    sprite->setFlipY(true);
    // the target holds final colours, but blending left its alpha below 1;
    // the scene is opaque, so copy it rather than blend the clear colour in.
    sprite->setBlendFunc({GL_ONE, GL_ZERO});
    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});
    if (size.width > 0.f && size.height > 0.f) {
        sprite->setScaleX(viewport.width / size.width);
        sprite->setScaleY(viewport.height / size.height);
    }
    addChild(sprite, 0);
    m_baked = sprite;
    if (m_status) m_status->setVisible(false);
    if (m_poster) {
        m_poster->removeFromParentAndCleanup(true);
        m_poster = nullptr;
    }
    return true;
}

void SceneWallpaperNode::updateClocks(bool force) {
    for (auto& clock : m_clocks) {
        auto const text = liveText(clock.kind, clock.use24, clock.seconds, clock.delimiter, clock.dateOrder, clock.dateYear);
        if (!force && text == clock.current) continue;
        clock.current = text;
        if (clock.drawable >= m_drawables.size()) continue;
        auto& d = m_drawables[clock.drawable];
        if (!d.label) continue;
        d.label->setString(text.c_str());
        auto const content = d.label->getContentSize();
        if (content.height > 0.f) {
            float fit = clock.boxH / content.height;
            if (clock.boxW > 1.f && content.width * fit > clock.boxW * 1.15f) fit = clock.boxW * 1.15f / content.width;
            d.fitX = d.fitY = fit;
            applyTransform(d);
        }
    }
}

void SceneWallpaperNode::tickClocks(float) {
    if (!visibleInHierarchy()) return;
    updateClocks(false);
}

// decoders open off the main thread; a Media Foundation open can take a
// noticeable fraction of a second.
void SceneWallpaperNode::startVideos() {
    for (size_t index : m_videos) {
        if (index >= m_drawables.size()) continue;
        auto const file = paimon::paths::toUtf8(m_drawables[index].videoFile);
        WeakRef<SceneWallpaperNode> weak = this;
        auto const generation = m_buildGeneration;
        paimon::ThreadTracker::get().spawn([weak, generation, index, file]() {
            geode::utils::thread::setName("SceneVideoOpen");
            if (paimon::isRuntimeShuttingDown()) return;
            paimon::video::VideoPlayerCreateOptions options;
            options.forceRGBA = true;
            options.enableAudio = false;
            std::shared_ptr<paimon::video::VideoPlayer> player = paimon::video::VideoPlayer::create(file, options);
            if (!player) {
                log::warn("[SceneWallpaper] could not open the video layer {}", file);
                return;
            }
            geode::Loader::get()->queueInMainThread([weak, generation, index, player]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                auto node = weak.lock();
                if (!node || node->m_buildGeneration != generation) return;
                node->attachVideo(index, std::move(player));
            });
        });
    }
}

void SceneWallpaperNode::attachVideo(size_t index, std::shared_ptr<paimon::video::VideoPlayer> player) {
    if (index >= m_drawables.size() || !player) return;
    auto& d = m_drawables[index];
    if (!d.sprite || d.video) return;
    player->setLoop(true);
    player->setVolume(0.f);
    d.video = std::move(player);
    d.videoStarted = false;
    d.videoPaused = false;
}

void SceneWallpaperNode::pauseVideos() {
    for (size_t index : m_videos) {
        if (index >= m_drawables.size()) continue;
        auto& d = m_drawables[index];
        if (!d.video || !d.videoStarted || d.videoPaused) continue;
        d.video->pause();
        d.videoPaused = true;
    }
}

void SceneWallpaperNode::pumpVideos(float dt) {
    float const csf = std::max(CCDirector::get()->getContentScaleFactor(), 0.01f);
    bool firstFrame = false;
    for (size_t index : m_videos) {
        if (index >= m_drawables.size()) continue;
        auto& d = m_drawables[index];
        if (!d.video || !d.sprite) continue;
        if (!d.videoStarted) {
            d.video->play();
            d.videoStarted = true;
        } else if (d.videoPaused) {
            d.video->resume();
            d.videoPaused = false;
        }
        d.video->update(dt);
        // the player uploads with raw glBindTexture calls; keep cocos' cache true.
        ccGLBindTexture2D(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        auto* texture = d.video->getCurrentFrameTexture();
        if (!texture || texture == d.sprite->getTexture()) continue;
        float const width = static_cast<float>(texture->getPixelsWide()) / csf;
        float const height = static_cast<float>(texture->getPixelsHigh()) / csf;
        if (width <= 0.f || height <= 0.f) continue;
        d.sprite->setTexture(texture);
        d.sprite->setTextureRect({0.f, 0.f, width, height});
        d.fitX = d.sizeX / width;
        d.fitY = d.sizeY / height;
        applyTransform(d);
        d.sprite->setVisible(true);
        if (!d.videoShown) {
            d.videoShown = true;
            firstFrame = true;
        }
    }
    if (firstFrame && !m_posterSaved) {
        bool const allShown = std::all_of(m_videos.begin(), m_videos.end(), [this](size_t index) {
            return index >= m_drawables.size() || m_drawables[index].videoShown;
        });
        if (allShown) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(posterPath(m_posterKey), ec)) queuePosterSave(renderSurface(true));
            else m_posterSaved = true;
        }
    }
}

void SceneWallpaperNode::update(float dt) {
    if (m_drawables.empty()) return;
    // a hidden wallpaper advances no timers and touches no transforms.
    if (!visibleInHierarchy()) {
        pauseVideos();
        return;
    }
    dt = std::isfinite(dt) ? std::clamp(dt, 0.f, 0.1f) : 0.f;
    if (!m_videos.empty()) pumpVideos(dt);

    if (m_parallax && m_world) {
        auto const pointer = convertToNodeSpace(geode::cocos::getMousePos());
        bool const moved = !m_hasPointer || std::abs(pointer.x - m_lastPointer.x) > 0.01f ||
            std::abs(pointer.y - m_lastPointer.y) > 0.01f;
        m_lastPointer = pointer;
        m_hasPointer = true;
        auto const viewport = getContentSize();
        float targetX = 0.f, targetY = 0.f;
        if (viewport.width > 0.f && viewport.height > 0.f) {
            targetX = std::clamp((pointer.x / viewport.width - 0.5f) * 2.f, -1.f, 1.f);
            targetY = std::clamp((pointer.y / viewport.height - 0.5f) * 2.f, -1.f, 1.f);
        }
        float const lerp = std::clamp(dt * m_parallaxRate, 0.f, 1.f);
        m_currentParallaxX += (targetX - m_currentParallaxX) * lerp;
        m_currentParallaxY += (targetY - m_currentParallaxY) * lerp;
        bool const settled = !moved && std::abs(targetX - m_currentParallaxX) < 0.0005f &&
            std::abs(targetY - m_currentParallaxY) < 0.0005f;
        if (!settled || !m_parallaxSettled) {
            m_parallaxSettled = settled;
            float const scaleX = std::max(std::abs(m_world->getScaleX()), 1e-4f);
            float const scaleY = std::max(std::abs(m_world->getScaleY()), 1e-4f);
            for (auto& d : m_drawables) {
                if (d.root < 0 || static_cast<size_t>(d.root) >= m_roots.size()) continue;
                auto const& depth = m_roots[static_cast<size_t>(d.root)];
                if (depth.x == 0.f && depth.y == 0.f) continue;
                // points -> scene units
                float const strength = m_strength * m_parallaxScale;
                float const ox = parallaxOffset(depth.x, m_currentParallaxX, strength) / scaleX;
                float const oy = parallaxOffset(depth.y, m_currentParallaxY, strength) / scaleY;
                d.offsetX = ox;
                d.offsetY = oy;
                applyTransform(d);
            }
        }
    }

    for (size_t index : m_animated) {
        auto& d = m_drawables[index];
        if (!d.sprite || d.animation.empty()) continue;
        auto const previous = d.frame;
        d.elapsed += dt;
        float duration = d.animation[d.frame].duration;
        size_t advances = 0;
        while (d.elapsed >= duration && advances++ < d.animation.size()) {
            d.elapsed -= duration;
            d.frame = (d.frame + 1) % d.animation.size();
            duration = d.animation[d.frame].duration;
        }
        if (d.frame == previous) continue;
        auto const& rect = d.animation[d.frame].rect;
        auto const before = d.sprite->getContentSize();
        d.sprite->setTextureRect(rect);
        if (std::abs(before.width - rect.size.width) > 0.01f || std::abs(before.height - rect.size.height) > 0.01f) {
            if (rect.size.width > 0.f && rect.size.height > 0.f && d.sizeX > 0.f && d.sizeY > 0.f) {
                d.fitX = d.sizeX / rect.size.width;
                d.fitY = d.sizeY / rect.size.height;
                applyTransform(d);
            }
        }
    }

    for (auto* particles : m_particles) particles->step(dt);
}

} // namespace paimon::wallpaper