#include "LiveSlotRuntime.hpp"

#include "LiveMaskCache.hpp"
#include "../data/GdResourcesLocator.hpp"
#include "../data/ImageBuffer.hpp"
#include "../data/PlistParser.hpp"
#include "../engine/LiveMaskAnalyzer.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>
#include <string_view>
#include <thread>

using namespace geode::prelude;

namespace paimon::texture_studio {
namespace {
#if defined(GEODE_IS_MOBILE)
constexpr std::size_t kMaskCacheBudget = 32 * 1024 * 1024;
#else
constexpr std::size_t kMaskCacheBudget = 64 * 1024 * 1024;
#endif
constexpr unsigned int kKeepRecentMaskFrames = 120;
// rgba4444 role masks: half the memory of rgba8888 at the same resolution.
constexpr std::size_t kMaskBytesPerTexel = 2;

struct MaskInput {
    std::string png, plist, base;
    bool forceTint = false;
    LiveMaskOptions options;
    TintScope scope;
    int maxDimension;
    std::filesystem::path cacheDir;
    unsigned threads;
};

struct MaskResult {
    std::vector<std::uint16_t> texels;
    int width = 0, height = 0;
    std::string error;
    bool cached = false;
};

unsigned analysisThreads() {
    // leave one core to the game; small machines analyze on the worker only.
    unsigned hw = std::thread::hardware_concurrency();
    return hw > 2 ? std::min(hw - 1, 8u) : 1u;
}

MaskResult buildLiveMask(MaskInput const& input, std::function<bool()> const& cancelled,
                         LiveSlotRuntime::Progress& progress) {
    MaskResult result;
    auto key = live_mask_cache::makeKey(input.png, input.plist, input.options,
        input.forceTint, static_cast<int>(input.scope));
    if (live_mask_cache::load(input.cacheDir, key, result.texels, result.width, result.height)) {
        result.cached = true;
        return result;
    }
    if (cancelled()) return {};

    std::vector<LiveFrameRect> rects;
    bool wholeImage = false;
    if (!input.plist.empty()) {
        auto parsed = PlistParser::parseFile(input.plist);
        if (!parsed) return {{}, 0, 0, parsed.unwrapErr()};
        for (auto const& frame : parsed.unwrap().frames) {
            if (!UiSpriteCatalog::shouldTint(
                    UiSpriteCatalog::classify(frame.name, input.base), input.scope)) continue;
            // rotated frames are analyzed in their transposed atlas slot.
            rects.push_back(frame.rotated
                ? LiveFrameRect{frame.rectX, frame.rectY, frame.rectH, frame.rectW}
                : LiveFrameRect{frame.rectX, frame.rectY, frame.rectW, frame.rectH});
        }
    } else {
        wholeImage = input.forceTint || UiSpriteCatalog::shouldTint(
            UiSpriteCatalog::classify(input.base, ""), input.scope);
    }
    if ((rects.empty() && !wholeImage) || cancelled()) return {};

    auto loaded = ImageBuffer::loadFromFile(input.png);
    if (!loaded) return {{}, 0, 0, loaded.unwrapErr()};
    auto atlas = std::move(loaded).unwrap();
    if (cancelled()) return {};
    if (atlas.width() > input.maxDimension || atlas.height() > input.maxDimension) {
        return {{}, 0, 0, fmt::format("UI atlas exceeds device limit ({})", input.maxDimension)};
    }
    if (wholeImage) rects.push_back({0, 0, atlas.width(), atlas.height()});

    progress.done = 0;
    progress.total = static_cast<int>(rects.size());
    result.texels.assign(atlas.pixelCount(), 0);
    if (!LiveMaskAnalyzer::analyzeAll(atlas.data(), atlas.width(), atlas.height(), std::move(rects),
            input.options, result.texels.data(), input.threads, cancelled, &progress.done, &progress.total)) {
        return {};
    }
    result.width = atlas.width();
    result.height = atlas.height();
    if (!cancelled()) live_mask_cache::store(input.cacheDir, key, result.texels, result.width, result.height);
    return result;
}

CCGLProgram* program() {
    return paimon::shaders::loadShader("paimon-live-slot-v2", "cell_vertex.glsl",
        "live_slot.glsl", nullptr, nullptr);
}

float finiteClamp(float value, float lo, float hi, float fallback) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

std::string resolve(std::string const& name) {
    std::string path = CCFileUtils::sharedFileUtils()->fullPathForFilename(name.c_str(), false);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return {};
    return path;
}

std::string normalizedPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
#ifdef GEODE_IS_WINDOWS
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return path;
}

}

LiveSlotRuntime& LiveSlotRuntime::get() {
    // leaked on purpose: cocos can outlive static destruction; gl frees before reload.
    static auto* runtime = new LiveSlotRuntime;
    return *runtime;
}

void LiveSlotRuntime::normalize(TextureProject& p) {
    p.liveRendering = true;
    // matches the editor slider; values past ~500 only darken everything.
    p.brightness = std::clamp(p.brightness, 20, 500);
    p.maskSoftness = finiteClamp(p.maskSoftness, 0.f, 1.f, .35f);
    p.clusterPrecision = std::clamp(p.clusterPrecision, 2, 10);
    p.edgeCleanup = std::clamp(p.edgeCleanup, 0, 4);
    p.outlineProtect = std::clamp(p.outlineProtect, 0, 255);
    p.saturation = finiteClamp(p.saturation, 0.f, 3.f, 1.f);
    p.contrast = finiteClamp(p.contrast, -1.f, 1.f, 0.f);
    p.tintStrength = finiteClamp(p.tintStrength, 0.f, 1.f, 1.f);
    p.glowStrength = finiteClamp(p.glowStrength, 0.f, 1.f, 1.f);
    p.toneSpread = finiteClamp(p.toneSpread, 0.f, 1.f, .6f);
    // live engine contract: every button and menu-ui sprite is painted from
    // the palette. Vanilla color families pick shades of it (LiveTones).
    p.tintScope = TintScope::ButtonsAndMenuUi;
    p.sheets.clear();
    p.representativeFrame.clear();
    p.representativeSheetIndex = -1;
    p.spriteSettings.clear();
    p.overrides.clear();
    p.autoCache.clear();
    p.hasBuiltOnce = false;
    p.lastBuiltAt = 0;
    p.lastZipRelPath.clear();
}

LiveTonePalette LiveSlotRuntime::tonesFor(TextureProject const& p) {
    LiveToneInputs inputs;
    inputs.primary = p.color1;
    inputs.secondary = p.color2;
    inputs.autoSurface = p.autoSurface;
    inputs.surface = p.colorSurface;
    inputs.autoBackground = p.autoBackground;
    inputs.background = p.colorBackground;
    inputs.spread = p.toneSpread;
    return buildLiveTones(inputs);
}

void LiveSlotRuntime::restoreSaved() {
    if (paimon::isRuntimeShuttingDown()) return;
    m_restored = true;
    auto& store = SlotStore::get();
    store.loadIndex();
    m_enabled = false;
    if (!Mod::get()->getSettingValue<bool>("texture-studio-enabled")) {
        releaseSources();
        log::info("[LiveSlot] restore skipped: texture-studio disabled");
        return;
    }
    if (store.activeSlotId().empty()) {
        releaseSources();
        log::info("[LiveSlot] restore skipped: no active slot");
        return;
    }
    auto loaded = store.loadSlot(store.activeSlotId());
    log::info("[LiveSlot] restore: active='{}' loaded={} live={}",
              store.activeSlotId(), static_cast<bool>(loaded),
              static_cast<bool>(loaded) && loaded.unwrap().liveRendering);
    if (loaded && loaded.unwrap().liveRendering) {
        if (auto result = preview(loaded.unwrap()); !result) m_error = result.unwrapErr();
    }
}

geode::Result<> LiveSlotRuntime::preview(TextureProject p) {
    if (paimon::isRuntimeShuttingDown()) return Err("Game is closing");
    if (!m_shader) {
        m_shader = program();
        if (m_shader) cacheShaderState();
    }
    if (!m_shader) return Err("Live color shader is unavailable on this GPU");
    if (m_cacheDir.empty()) m_cacheDir = SlotPaths::rootDir() / "live-cache";
    checkResourceSources();
    normalize(p);
    bool rebuild = m_sources.empty() || p.maskSoftness != m_project.maskSoftness ||
        p.clusterPrecision != m_project.clusterPrecision ||
        p.edgeCleanup != m_project.edgeCleanup || p.tintScope != m_project.tintScope;
    m_project = std::move(p);
    m_tones = tonesFor(m_project);
    m_uniformsDirty = true;
    m_enabled = true;
    m_error.clear();
    bool wasEmpty = m_sources.empty();
    if (wasEmpty) collectSources();
    if (m_sources.empty()) {
        m_enabled = false;
        return Err("No local UI textures found");
    }
    if (wasEmpty) log::info("[LiveSlot] preview: {} sources collected", m_sources.size());
    if (rebuild) prepareMasks();
    else requestMaskBuild();
    return Ok();
}

geode::Result<> LiveSlotRuntime::activate(TextureProject const& project) {
    auto previous = m_project;
    bool wasEnabled = m_enabled;
    GEODE_UNWRAP(preview(project));
    if (auto saved = SlotStore::get().setActiveSlot(project.id); !saved) {
        if (wasEnabled) (void)preview(previous);
        else m_enabled = false;
        return saved;
    }
    return Ok();
}

geode::Result<> LiveSlotRuntime::reanalyze(TextureProject project) {
    m_enabled = false;
    releaseSources();
    if (m_cacheDir.empty()) m_cacheDir = SlotPaths::rootDir() / "live-cache";
    live_mask_cache::clear(m_cacheDir);
    m_cacheHits = 0;
    return preview(std::move(project));
}

geode::Result<> LiveSlotRuntime::disable() {
    GEODE_UNWRAP(SlotStore::get().setActiveSlot(""));
    m_enabled = false;
    releaseSources();
    return Ok();
}

void LiveSlotRuntime::releaseSources() {
    ++*m_generation;
    unbindMasks();
    m_sources.clear();
    m_sourcesByName.clear();
    m_sourcesByPath.clear();
    m_textures.clear();
    m_maskBytes = 0;
    m_pending = false;
    m_cachedTextureCount = 0;
    m_lastCacheCheckFrame = std::numeric_limits<unsigned int>::max();
    m_lastMaskTrimFrame = 0;
    m_textureRefreshQueued = false;
    m_maskBuildQueued = false;
    // a cancelled worker owns no cocos objects. its completion releases the
    // worker slot before starting any newer generation, including GL reloads.
}

void LiveSlotRuntime::checkResourceSources() {
    auto* files = CCFileUtils::sharedFileUtils();
    auto* director = CCDirector::get();
    if (!files || !director) return;

    auto const packCount = files->getTexturePackCount();
    auto const contentScale = director->getContentScaleFactor();
    auto const& paths = files->getSearchPaths();
    bool const samePaths = paths.size() == m_sourceSearchPaths.size()
        && std::equal(paths.begin(), paths.end(), m_sourceSearchPaths.begin(),
            [](auto const& path, std::string const& saved) {
                return std::string_view(path.c_str(), path.size()) == saved;
            });
    if (packCount == m_sourceTexturePackCount && contentScale == m_sourceContentScale && samePaths) return;

    // replacing a pack can change its paths while the pack count stays constant.
    releaseSources();
    m_sourceTexturePackCount = packCount;
    m_sourceContentScale = contentScale;
    m_sourceSearchPaths.clear();
    m_sourceSearchPaths.reserve(paths.size());
    for (auto const& path : paths) m_sourceSearchPaths.emplace_back(path.c_str(), path.size());
}

void LiveSlotRuntime::collectSources() {
    std::set<std::string> seen;
    auto add = [&](std::string const& base, bool sheet, SourceKind kind = SourceKind::Masked,
                   bool forceTint = false) {
        auto png = resolve(base + ".png");
        auto plist = sheet ? resolve(base + ".plist") : std::string();
        if (png.empty() || (sheet && plist.empty()) || !seen.insert(png).second) return;
        Source source;
        source.png = std::move(png);
        source.plist = std::move(plist);
        source.base = base;
        source.kind = kind;
        source.forceTint = forceTint;
        auto index = m_sources.size();
        m_sourcesByPath.emplace(normalizedPath(source.png), index);
        for (auto const& suffix : {".png", "-hd.png", "-uhd.png"}) {
            m_sourcesByName.emplace(normalizedPath(base + suffix), index);
        }
        m_sources.push_back(std::move(source));
    };
    for (auto const* base : {
        "GJ_GameSheet03",
        "GJ_GameSheet04",
        "GJ_GameSheetEditor",
        "GJ_LaunchSheet",
        "GJ_LaunchSheet2",
        "GJ_ShopSheet",
        "GauntletSheet",
        "TowerSheet",
        "SecretSheet",
        "WorldSheet",
        "TreasureRoomSheet",
        "DungeonSheet"
    }) add(base, true);
    if (auto sheets = GdResourcesLocator::detectVanillaSheets()) {
        for (auto const& sheet : sheets.unwrap()) {
            if (!UiSpriteCatalog::isGameplaySheet(sheet.baseName)) add(sheet.baseName, true);
        }
    }
    for (auto const* name : {
        "GJ_button_01", "GJ_button_02", "GJ_button_03",
        "GJ_button_04", "GJ_button_05", "GJ_button_06",
        "GJ_button_07", "GJ_button_08", "GJ_button_09",
        "GJ_button_10", "GJ_button_11", "GJ_button_12",
        "GJ_button_13", "GJ_button_14", "GJ_button_15",
        "slidergroove", "slidergroove2", "slidergroove_02",
        "sliderBar", "sliderBar2", "sliderthumb", "sliderthumbsel",
        "loadingCircle", "smallDot", "progressBar",
        "edit_barBG_001",
        "square02_001", "square02b_001", "square02b_002",
        "square02c_001", "square02d_001", "square04_001"
    }) add(name, false);
    // popup and panel fills (brown, blue, green, purple, gray): menu chrome
    // for live painting even though packs keep them vanilla.
    for (auto const* name : {
        "GJ_square01", "GJ_square02", "GJ_square03",
        "GJ_square04", "GJ_square05", "GJ_square06"
    }) add(name, false, SourceKind::Masked, true);
    add("GJ_gradientBG", false, SourceKind::Background);
    // main menu art is shared with levels: painted only outside gameplay.
    for (int i = 1; i <= 59; ++i) {
        add(fmt::format("game_bg_{:02}_001", i), false, SourceKind::MenuBackground);
    }
    for (int i = 1; i <= 22; ++i) {
        add(fmt::format("groundSquare_{:02}_001", i), false, SourceKind::MenuBackground);
        add(fmt::format("groundSquare_{:02}_2_001", i), false, SourceKind::MenuBackground);
    }
    refreshTextures();
}

void LiveSlotRuntime::refreshTextures() {
    m_textures.clear();
    auto* cache = CCTextureCache::sharedTextureCache();
    if (!cache) return;
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        std::erase_if(source.textures, [](auto const& texture) {
            return texture->retainCount() == 1;
        });
        // keep instances still displayed after removal from the texture cache.
        for (auto const& texture : source.textures) m_textures[texture.data()] = i;
    }
    if (cache->m_pTextures) {
        for (auto [key, texture] : CCDictionaryExt<std::string_view, CCTexture2D*>(cache->m_pTextures)) {
            auto path = std::string(key);
            onTextureLoaded(path.c_str(), texture, true);
        }
    }
    m_cachedTextureCount = cache->m_pTextures ? cache->m_pTextures->count() : 0;
}

void LiveSlotRuntime::requestTextureRefresh() {
    if (m_textureRefreshQueued || m_sources.empty()) return;
    m_textureRefreshQueued = true;
    auto generation = m_generation;
    auto ticket = generation->load();
    paimon::scheduleMainThreadDelay(.001f, [generation, ticket] {
        if (generation->load() != ticket) return;
        auto& runtime = get();
        runtime.m_textureRefreshQueued = false;
        runtime.refreshTextures();
    });
}

void LiveSlotRuntime::onTextureLoaded(char const* path, CCTexture2D* texture, bool skipSuffix) {
    if (!path || !texture || m_sources.empty() || m_textures.contains(texture)) return;
    std::string_view filename(path);
    auto slash = filename.find_last_of("/\\");
    if (slash != std::string_view::npos) filename.remove_prefix(slash + 1);
    auto named = m_sourcesByName.find(normalizedPath(std::string(filename)));
    if (named == m_sourcesByName.end()) return;
    auto index = named->second;
    auto key = normalizedPath(path);
    auto direct = m_sourcesByPath.find(key);
    if (direct == m_sourcesByPath.end() || direct->second != index) {
        auto* files = CCFileUtils::sharedFileUtils();
        auto resolved = normalizedPath(files->fullPathForFilename(path, skipSuffix));
        auto preferred = normalizedPath(files->fullPathForFilename(path, false));
        auto sourcePath = normalizedPath(m_sources[index].png);
        if (resolved != sourcePath && preferred != sourcePath && key != sourcePath) return;
    }
    auto& source = m_sources[index];
    auto size = texture->getContentSizeInPixels();
    if (source.mask && (size.width != source.width || size.height != source.height)) return;
    source.textures.emplace_back(texture);
    m_textures.emplace(texture, index);
    // register before a mask exists. drawing this texture requests its analysis.
}

void LiveSlotRuntime::prepareMasks() {
    ++*m_generation;
    m_maskBuildQueued = false;
    m_textureRefreshQueued = false;
    m_maskBytes = 0;
    m_failures = 0;
    unbindMasks();
    auto frame = CCDirector::get()->getTotalFrames();
    for (auto& source : m_sources) {
        source.mask = nullptr;
        source.maskBytes = 0;
        source.analyzed = false;
        source.requested = source.requested && frame - source.lastUsedFrame <= 2;
    }
    requestMaskBuild();
}

void LiveSlotRuntime::requestMaskBuild() {
    if (!m_enabled || paimon::isRuntimeShuttingDown()) return;
    bool requested = std::any_of(m_sources.begin(), m_sources.end(), [](auto const& source) {
        return source.requested && !source.analyzed && !source.textures.empty();
    });
    m_pending = requested || m_workerRunning;
    if (!requested || m_workerRunning || m_maskBuildQueued) return;
    m_maskBuildQueued = true;
    auto generation = m_generation;
    auto ticket = generation->load();
    // defer thread creation from draw(), and separate consecutive uploads by
    // scheduler ticks. There is at most one decoded result awaiting upload.
    paimon::scheduleMainThreadDelay(.001f, [generation, ticket] {
        if (generation->load() != ticket) return;
        auto& runtime = get();
        runtime.m_maskBuildQueued = false;
        runtime.startNextMask();
    });
}

void LiveSlotRuntime::startNextMask() {
    if (m_workerRunning || !m_enabled || paimon::isRuntimeShuttingDown()) return;
    // smallest texture first: buttons and panels paint while big atlases analyze.
    auto next = m_sources.end();
    double nextArea = 0.0;
    for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
        if (!it->requested || it->analyzed || it->textures.empty()) continue;
        auto* texture = it->textures.front().data();
        double area = static_cast<double>(texture->getPixelsWide()) * texture->getPixelsHigh();
        if (next == m_sources.end() || area < nextArea) {
            next = it;
            nextArea = area;
        }
    }
    if (next == m_sources.end()) {
        m_pending = false;
        m_analyzing.clear();
        return;
    }
    auto index = static_cast<std::size_t>(next - m_sources.begin());
    LiveMaskOptions options;
    options.softness = m_project.maskSoftness;
    options.clusterPrecision = m_project.clusterPrecision;
    options.edgeCleanup = m_project.edgeCleanup;
    MaskInput input{next->png, next->plist, next->base, next->forceTint, options,
                    m_project.tintScope, m_maxTextureDimension, m_cacheDir, analysisThreads()};
    auto generation = m_generation;
    auto ticket = generation->load();
    m_progress = std::make_shared<Progress>();
    m_analyzing = next->base;
    m_workerRunning = true;
    m_pending = true;
    bool started = false;
    try {
        started = paimon::ThreadTracker::get().spawn(
            [input = std::move(input), generation, ticket, index, progress = m_progress] {
            auto cancelled = [&] {
                return paimon::isRuntimeShuttingDown() || generation->load() != ticket;
            };
            MaskResult result;
            try { result = buildLiveMask(input, cancelled, *progress); }
            catch (std::exception const& error) { result = {}; result.error = error.what(); }
            catch (...) { result = {}; result.error = "Texture analysis failed"; }
            // the decoded atlas and all scratch planes die on the worker. only
            // the packed 16-bit mask crosses to the UI thread.
            Loader::get()->queueInMainThread([generation, ticket, index, result = std::move(result)] {
                auto& runtime = get();
                runtime.m_workerRunning = false;
                if (paimon::isRuntimeShuttingDown()) return;
                if (generation->load() == ticket && index < runtime.m_sources.size()) {
                    auto& source = runtime.m_sources[index];
                    source.analyzed = true;
                    if (!result.texels.empty() && result.width > 0 && result.height > 0) {
                        auto bytes = result.texels.size() * kMaskBytesPerTexel;
                        auto frame = CCDirector::get()->getTotalFrames();
                        runtime.trimUnusedMasks(frame, bytes);
                        auto* mask = new CCTexture2D();
                        if (mask->initWithData(result.texels.data(), kCCTexture2DPixelFormat_RGBA4444,
                                static_cast<unsigned int>(result.width), static_cast<unsigned int>(result.height),
                                CCSize(static_cast<float>(result.width), static_cast<float>(result.height)))) {
                            mask->autorelease();
                            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            mask->setTexParameters(&params);
                            source.mask = mask;
                            source.width = result.width;
                            source.height = result.height;
                            source.maskBytes = bytes;
                            runtime.m_maskBytes += bytes;
                            if (result.cached) ++runtime.m_cacheHits;
                        } else {
                            mask->release();
                            ++runtime.m_failures;
                        }
                    }
                    if (!result.error.empty()) {
                        ++runtime.m_failures;
                        log::warn("[LiveSlot] {}: {}", source.base, result.error);
                    }
                }
                // also resumes a newer generation after cancellation. never
                // overlap workers when sliders are changed rapidly.
                runtime.requestMaskBuild();
                if (!runtime.m_pending) {
                    runtime.m_analyzing.clear();
                    log::info("[LiveSlot] masks done: {} MiB cached, {} from disk, {} failures",
                        runtime.m_maskBytes / (1024 * 1024), runtime.m_cacheHits, runtime.m_failures);
                }
            });
        });
    } catch (std::exception const& error) {
        log::warn("[LiveSlot] Cannot start analysis: {}", error.what());
    }
    if (!started) {
        m_workerRunning = false;
        m_sources[index].analyzed = true;
        ++m_failures;
        requestMaskBuild();
    }
}

void LiveSlotRuntime::trimUnusedMasks(unsigned int frame, std::size_t incomingBytes) {
    // soft budget: keep visible/recent masks to avoid flicker or rebuild loops.
    // full-resolution role masks preserve boundaries and palette accuracy.
    while (m_maskBytes + incomingBytes > kMaskCacheBudget) {
        auto oldest = m_sources.end();
        for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
            if (!it->mask || frame - it->lastUsedFrame < kKeepRecentMaskFrames) continue;
            if (oldest == m_sources.end() || frame - it->lastUsedFrame > frame - oldest->lastUsedFrame) {
                oldest = it;
            }
        }
        if (oldest == m_sources.end()) break;
        unbindMasks();
        m_maskBytes -= std::min(m_maskBytes, oldest->maskBytes);
        oldest->maskBytes = 0;
        oldest->mask = nullptr;
        oldest->analyzed = false;
        oldest->requested = false;
    }
}

void LiveSlotRuntime::requestMaskTrim(unsigned int frame) {
    if (m_maskBytes <= kMaskCacheBudget || frame - m_lastMaskTrimFrame < kKeepRecentMaskFrames) return;
    m_lastMaskTrimFrame = frame;
    auto generation = m_generation;
    auto ticket = generation->load();
    paimon::scheduleMainThreadDelay(.001f, [generation, ticket] {
        if (generation->load() != ticket) return;
        get().trimUnusedMasks(CCDirector::get()->getTotalFrames(), 0);
    });
}

void LiveSlotRuntime::unbindMasks() {
    // cocos forgets deleted textures only for unit 0. Reset unit 1 before a
    // mask dies, or a reused GL name would skip its next bind and stay unpainted.
    if (!m_shader) return;
    ccGLBindTexture2DN(1, 0);
    glActiveTexture(GL_TEXTURE0);
}

void LiveSlotRuntime::cacheShaderState() {
    constexpr std::array names{
        "u_texture", "u_roleMask",
        "u_tonePrimary", "u_toneSecondary", "u_toneBlue", "u_tonePink", "u_toneRed",
        "u_toneSurface", "u_toneNeutral", "u_toneBackground",
        "u_detailColor", "u_glowColor", "u_brightness", "u_saturation", "u_contrast",
        "u_darkThreshold", "u_glowReplace", "u_applyDetail", "u_strength", "u_glowStrength",
        "u_premultiplied", "u_maskScaleX", "u_maskScaleY", "u_fullPaint"
    };
    static_assert(names.size() == static_cast<std::size_t>(Uniform::Count));
    for (std::size_t i = 0; i < names.size(); ++i) {
        m_uniformLocations[i] = m_shader->getUniformLocationForName(names[i]);
    }
    auto* cache = CCShaderCache::sharedShaderCache();
    m_spriteShaders = {cache->programForKey(kCCShader_PositionTextureColor),
        cache->programForKey(kCCShader_PositionTextureColorAlphaTest),
        cache->programForKey(kCCShader_PositionTexture_uColor)};
    GLint maxDimension = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxDimension);
    m_maxTextureDimension = maxDimension > 0 ? std::min(maxDimension, 4096) : 2048;
    m_uniformsDirty = true;
}

void LiveSlotRuntime::updateUniforms(CCTexture2D* texture, Source const& source, bool fullPaint) {
    auto* shader = m_shader.data();
    auto location = [&](Uniform uniform) { return m_uniformLocations[static_cast<std::size_t>(uniform)]; };
    auto scalar = [&](Uniform uniform, float value) {
        shader->setUniformLocationWith1f(location(uniform), value);
    };
    auto color = [&](Uniform uniform, ccColor3B value) {
        shader->setUniformLocationWith3f(location(uniform), value.r, value.g, value.b);
    };
    if (m_uniformsDirty) {
        shader->setUniformLocationWith1i(location(Uniform::Texture), 0);
        shader->setUniformLocationWith1i(location(Uniform::RoleMask), 1);
        auto tone = [&](LiveTone t) { return m_tones[static_cast<std::size_t>(t)]; };
        color(Uniform::TonePrimary, tone(LiveTone::Primary));
        color(Uniform::ToneSecondary, tone(LiveTone::Secondary));
        color(Uniform::ToneBlue, tone(LiveTone::Blue));
        color(Uniform::TonePink, tone(LiveTone::Pink));
        color(Uniform::ToneRed, tone(LiveTone::Red));
        color(Uniform::ToneSurface, tone(LiveTone::Surface));
        color(Uniform::ToneNeutral, tone(LiveTone::Neutral));
        color(Uniform::ToneBackground, tone(LiveTone::Background));
        color(Uniform::DetailColor, m_project.colorDetail);
        color(Uniform::GlowColor, m_project.colorGlow);
        scalar(Uniform::Brightness, m_project.brightness);
        scalar(Uniform::Saturation, m_project.saturation);
        scalar(Uniform::Contrast, m_project.contrast);
        scalar(Uniform::DarkThreshold, m_project.outlineProtect);
        scalar(Uniform::GlowReplace, m_project.alternativeGlowOverlay ? 1.f : 0.f);
        scalar(Uniform::ApplyDetail, m_project.colorDetail.r != 255 || m_project.colorDetail.g != 255 ||
            m_project.colorDetail.b != 255 ? 1.f : 0.f);
        scalar(Uniform::Strength, m_project.tintStrength);
        scalar(Uniform::GlowStrength, m_project.glowStrength);
    }
    bool premultiplied = texture->hasPremultipliedAlpha();
    if (m_uniformsDirty || premultiplied != m_premultiplied) {
        scalar(Uniform::Premultiplied, premultiplied ? 1.f : 0.f);
        m_premultiplied = premultiplied;
    }
    if (m_uniformsDirty || fullPaint != m_fullPaint) {
        scalar(Uniform::FullPaint, fullPaint ? 1.f : 0.f);
        m_fullPaint = fullPaint;
    }
    // only masked draws read the mask scale, but a reset must still write it:
    // gl starts the uniform at 0 while the cache holds 1. a full-paint draw
    // first (gradient background) would otherwise leave it at 0 for good and
    // every masked sprite would sample texel (0, 0) and stay vanilla.
    float maskScaleX = m_maskScaleX;
    float maskScaleY = m_maskScaleY;
    if (!fullPaint && source.width > 0 && source.height > 0) {
        maskScaleX = static_cast<float>(texture->getPixelsWide()) / source.width;
        maskScaleY = static_cast<float>(texture->getPixelsHigh()) / source.height;
    }
    if (m_uniformsDirty || maskScaleX != m_maskScaleX || maskScaleY != m_maskScaleY) {
        scalar(Uniform::MaskScaleX, maskScaleX);
        scalar(Uniform::MaskScaleY, maskScaleY);
        m_maskScaleX = maskScaleX;
        m_maskScaleY = maskScaleY;
    }
    m_uniformsDirty = false;
}

void LiveSlotRuntime::beginFrame(unsigned int frame) {
    if (m_lastCacheCheckFrame == frame) return;
    m_lastCacheCheckFrame = frame;
    requestMaskTrim(frame);
    auto* textureCache = CCTextureCache::sharedTextureCache();
    if (textureCache && textureCache->m_pTextures &&
        textureCache->m_pTextures->count() != m_cachedTextureCount) {
        requestTextureRefresh();
    }
    // level art (backgrounds, grounds, effect layers) keeps its colors in play.
    auto* gm = GameManager::get();
    m_inGameplay = gm && (gm->m_playLayer || gm->m_levelEditorLayer);
}

CCGLProgram* LiveSlotRuntime::prepareDraw(CCTexture2D* texture, CCGLProgram* original) {
    if (!m_enabled || !texture || m_project.tintStrength <= 0.f || paimon::isRuntimeShuttingDown()) return nullptr;
    auto frame = CCDirector::get()->getTotalFrames();
    beginFrame(frame);
    auto it = m_textures.find(texture);
    if (it == m_textures.end()) return nullptr;
    if (original != m_spriteShaders[0].data() && original != m_spriteShaders[1].data() &&
        original != m_spriteShaders[2].data()) return nullptr;
    auto& source = m_sources[it->second];
    source.lastUsedFrame = frame;
    bool const fullPaint = source.kind != SourceKind::Masked;
    if (fullPaint) {
        if (!m_project.livePaintBackgrounds) return nullptr;
        if (source.kind == SourceKind::MenuBackground && m_inGameplay) return nullptr;
    } else {
        if (!source.mask) {
            if (!source.requested && !source.analyzed) {
                source.requested = true;
                requestMaskBuild();
            }
            return nullptr;
        }
        auto size = texture->getContentSizeInPixels();
        if (size.width != source.width || size.height != source.height) return nullptr;
    }
    if (!m_loggedTintApplied) {
        m_loggedTintApplied = true;
        log::info("[LiveSlot] tint applied (first texture: {})", source.base);
    }
    auto* shader = m_shader.data();
    if (!shader) return nullptr;
    shader->use();
    updateUniforms(texture, source, fullPaint);
    if (!fullPaint) {
        ccGLBindTexture2DN(1, source.mask->getName());
        glActiveTexture(GL_TEXTURE0);
    }
    return shader;
}

bool LiveSlotRuntime::remapLayerColors(ccColor4F* colors, int count) {
    if (!m_enabled || !colors || count <= 0 || !m_project.livePaintLists ||
        m_project.tintStrength <= 0.f || paimon::isRuntimeShuttingDown()) return false;
    beginFrame(CCDirector::get()->getTotalFrames());
    if (m_inGameplay) return false;
    auto const surface = m_tones[static_cast<std::size_t>(LiveTone::Surface)];
    float const strength = m_project.tintStrength;
    auto toByte = [](float v) {
        return static_cast<GLubyte>(std::clamp(static_cast<int>(std::lround(v * 255.f)), 0, 255));
    };
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        auto& c = colors[i];
        ccColor3B source{toByte(c.r), toByte(c.g), toByte(c.b)};
        auto hsv = paimon::icons::math::toHSV(source);
        // only gd's brown/orange fills (list backgrounds, cells): other
        // colored layers are overlays or color previews with meaning.
        if (liveToneFamily(hsv.h, hsv.s, hsv.v) != LiveTone::Surface) continue;
        auto painted = liveTintColor(source, surface, m_project.brightness,
            m_project.saturation, m_project.contrast);
        c.r = (source.r + (painted.r - source.r) * strength) / 255.f;
        c.g = (source.g + (painted.g - source.g) * strength) / 255.f;
        c.b = (source.b + (painted.b - source.b) * strength) / 255.f;
        changed = true;
    }
    return changed;
}

void LiveSlotRuntime::onGLContextReload() {
    m_enabled = false;
    m_restored = false;
    releaseSources();
    m_sourceTexturePackCount = 0;
    m_sourceContentScale = 0.f;
    m_sourceSearchPaths.clear();
    m_shader = nullptr;
    m_spriteShaders = {};
    m_uniformLocations = {};
    m_uniformsDirty = true;
    m_loggedTintApplied = false;
}

std::string LiveSlotRuntime::status() const {
    if (!m_error.empty()) return m_error;
    if (!m_enabled) return "Original colors";
    auto masks = std::count_if(m_sources.begin(), m_sources.end(), [](auto const& source) {
        return source.mask != nullptr;
    });
    if (m_pending) {
        int total = m_progress->total.load();
        int done = m_progress->done.load();
        if (!m_analyzing.empty() && total > 0) {
            return fmt::format("Analyzing {} - {}%", m_analyzing, std::clamp(done * 100 / total, 0, 100));
        }
        return fmt::format("Analyzing - {} ready", masks);
    }
    if (m_failures) return fmt::format("Live - {} textures unavailable", m_failures);
    return fmt::format("Live - {} textures / {:.1f} MiB", masks, m_maskBytes / (1024.f * 1024.f));
}

float LiveSlotRuntime::progress() const {
    if (!m_enabled || !m_pending) return -1.f;
    int total = m_progress->total.load();
    if (total <= 0) return 0.f;
    return std::clamp(static_cast<float>(m_progress->done.load()) / total, 0.f, 1.f);
}

std::uintmax_t LiveSlotRuntime::diskCacheBytes() const {
    return live_mask_cache::sizeOnDisk(m_cacheDir.empty() ? SlotPaths::rootDir() / "live-cache" : m_cacheDir);
}

}
