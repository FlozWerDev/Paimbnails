#include "LiveSlotRuntime.hpp"

#include "../data/GdResourcesLocator.hpp"
#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../persist/SlotStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <algorithm>
#include <cmath>
#include <set>

using namespace geode::prelude;

namespace paimon::texture_studio {
namespace {
CCGLProgram* program() {
    return paimon::shaders::loadShader("paimon-live-slot-v1", "cell_vertex.glsl",
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
}

LiveSlotRuntime& LiveSlotRuntime::get() {
    // leaked on purpose: cocos can outlive static destruction; gl frees before reload.
    static auto* runtime = new LiveSlotRuntime;
    return *runtime;
}

void LiveSlotRuntime::normalize(TextureProject& p) {
    p.liveRendering = true;
    p.brightness = std::clamp(p.brightness, 1, 1000);
    p.maskSoftness = finiteClamp(p.maskSoftness, 0.f, 1.f, .35f);
    p.clusterPrecision = std::clamp(p.clusterPrecision, 2, 10);
    p.edgeCleanup = std::clamp(p.edgeCleanup, 0, 4);
    p.outlineProtect = std::clamp(p.outlineProtect, 0, 255);
    p.saturation = finiteClamp(p.saturation, 0.f, 3.f, 1.f);
    p.contrast = finiteClamp(p.contrast, -1.f, 1.f, 0.f);
    p.tintStrength = finiteClamp(p.tintStrength, 0.f, 1.f, 1.f);
    p.glowStrength = finiteClamp(p.glowStrength, 0.f, 1.f, 1.f);
    p.tintScope = p.tintScope == TintScope::ButtonsOnly
        ? TintScope::ButtonsOnly : TintScope::ButtonsAndMenuUi;
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

void LiveSlotRuntime::restoreSaved() {
    if (paimon::isRuntimeShuttingDown()) return;
    m_restored = true;
    auto& store = SlotStore::get();
    store.loadIndex();
    m_enabled = false;
    if (!Mod::get()->getSettingValue<bool>("texture-studio-enabled")) return;
    if (store.activeSlotId().empty()) return;
    auto loaded = store.loadSlot(store.activeSlotId());
    if (loaded && loaded.unwrap().liveRendering) {
        if (auto result = preview(loaded.unwrap()); !result) m_error = result.unwrapErr();
    }
}

geode::Result<> LiveSlotRuntime::preview(TextureProject p) {
    if (paimon::isRuntimeShuttingDown()) return Err("Game is closing");
    if (!m_shader) m_shader = program();
    if (!m_shader) return Err("Pack Gen shader is unavailable on this GPU");
    normalize(p);
    bool rebuild = m_sources.empty() || p.maskSoftness != m_project.maskSoftness ||
        p.clusterPrecision != m_project.clusterPrecision ||
        p.edgeCleanup != m_project.edgeCleanup || p.tintScope != m_project.tintScope;
    m_project = std::move(p);
    m_uniformsDirty = true;
    m_enabled = true;
    m_error.clear();
    if (m_sources.empty()) collectSources();
    if (m_sources.empty()) {
        m_enabled = false;
        return Err("No local UI textures found");
    }
    if (rebuild) prepareMasks();
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

geode::Result<> LiveSlotRuntime::disable() {
    GEODE_UNWRAP(SlotStore::get().setActiveSlot(""));
    m_enabled = false;
    return Ok();
}

void LiveSlotRuntime::collectSources() {
    std::set<std::string> seen;
    auto add = [&](std::string const& base, bool sheet) {
        auto png = resolve(base + ".png");
        auto plist = sheet ? resolve(base + ".plist") : std::string();
        if (png.empty() || (sheet && plist.empty()) || !seen.insert(png).second) return;
        Source source;
        source.png = std::move(png);
        source.plist = std::move(plist);
        source.base = base;
        m_sources.push_back(std::move(source));
    };
    if (auto sheets = GdResourcesLocator::detectVanillaSheets()) {
        for (auto const& sheet : sheets.unwrap()) {
            if (!UiSpriteCatalog::isGameplaySheet(sheet.baseName)) add(sheet.baseName, true);
        }
    }
    for (auto const* base : {"GJ_GameSheet03", "GJ_GameSheet04", "GJ_LaunchSheet", "GJ_LaunchSheet2"}) add(base, true);
    for (auto const* name : {"GJ_button_01", "GJ_button_02", "GJ_button_03",
            "GJ_button_04", "GJ_button_05", "GJ_button_06", "GJ_button_07",
            "GJ_button_08", "GJ_button_09", "GJ_button_10", "GJ_button_11",
            "GJ_button_12", "GJ_button_13", "GJ_button_14", "GJ_button_15",
            "slidergroove", "sliderBar", "sliderthumb", "sliderthumbsel",
            "loadingCircle", "smallDot", "progressBar"}) add(name, false);
    refreshTextures();
}

void LiveSlotRuntime::refreshTextures() {
    m_textures.clear();
    auto* cache = CCTextureCache::sharedTextureCache();
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        source.texture = nullptr;
        if (!source.mask) continue;
        source.texture = cache->textureForKey(source.png.c_str());
        if (!source.texture) source.texture = cache->textureForKey((source.base + ".png").c_str());
        if (source.texture) m_textures[source.texture.data()] = i;
    }
}

bool LiveSlotRuntime::cellColor(int row, ccColor3B* out) {
    if (!m_enabled || !out || paimon::isRuntimeShuttingDown()) return false;
    if (m_project.tintScope != TintScope::ButtonsAndMenuUi) return false;
    *out = (row & 1) ? m_project.color2 : m_project.color1;
    return true;
}

void LiveSlotRuntime::onTextureLoaded(char const* path, CCTexture2D* texture, bool skipSuffix) {
    if (!path || !texture || m_sources.empty()) return;
    std::string resolved = CCFileUtils::sharedFileUtils()->fullPathForFilename(path, skipSuffix);
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        auto& source = m_sources[i];
        if (!source.mask || resolved != source.png) continue;
        if (source.texture) m_textures.erase(source.texture.data());
        source.texture = texture;
        m_textures[texture] = i;
        break;
    }
}

void LiveSlotRuntime::prepareMasks() {
    struct Input { std::string png, plist, base; };
    std::vector<Input> inputs;
    for (auto& s : m_sources) {
        inputs.push_back({s.png, s.plist, s.base});
        s.mask = nullptr;
    }
    m_pending = true;
    m_failures = 0;
    auto generation = m_generation;
    auto ticket = ++*generation;
    SpritePreviewOptions options;
    options.maskSoftness = m_project.maskSoftness;
    options.clusterPrecision = m_project.clusterPrecision;
    options.edgeCleanup = m_project.edgeCleanup;
    auto scope = m_project.tintScope;
    bool started = paimon::ThreadTracker::get().spawn(
        [inputs = std::move(inputs), options, scope, generation, ticket]() {
        auto cancelled = [&] {
            return paimon::isRuntimeShuttingDown() || generation->load() != ticket;
        };
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            if (cancelled()) return;
            std::shared_ptr<ImageBuffer> packed;
            std::string error;
            try {
                auto const& input = inputs[index];
                std::vector<SpriteFrameInfo> frames;
                bool selected = UiSpriteCatalog::shouldTint(
                    UiSpriteCatalog::classify(input.base, ""), scope);
                if (!input.plist.empty()) {
                    auto parsed = PlistParser::parseFile(input.plist);
                    if (!parsed) error = parsed.unwrapErr();
                    else {
                        for (auto const& frame : parsed.unwrap().frames) {
                            if (UiSpriteCatalog::shouldTint(
                                    UiSpriteCatalog::classify(frame.name, input.base), scope)) {
                                frames.push_back(frame);
                            }
                        }
                    }
                    selected = !frames.empty();
                }
                if (selected && error.empty()) {
                    auto loaded = ImageBuffer::loadFromFile(input.png);
                    if (!loaded) error = loaded.unwrapErr();
                    else {
                        auto atlas = std::move(loaded).unwrap();
                        // big masks would pin too much gpu for one ui sheet.
                        if (atlas.width() > 4096 || atlas.height() > 4096) {
                            error = "UI atlas exceeds 4096 pixels";
                        } else if (input.plist.empty()) {
                            packed = std::make_shared<ImageBuffer>(SpritePreviewRenderer::renderRoleMask(
                                SpritePreviewRenderer::renderMasks(atlas, options).masks));
                        } else {
                            packed = std::make_shared<ImageBuffer>(atlas.width(), atlas.height());
                            for (auto const& frame : frames) {
                                if (cancelled()) return;
                                auto pixels = SpritesheetReader::extractFrame(atlas, frame);
                                auto mask = SpritePreviewRenderer::renderRoleMask(
                                    SpritePreviewRenderer::renderMasks(pixels, options).masks);
                                if (frame.rotated) mask.rotateCW90();
                                packed->blitOverwrite(frame.rectX, frame.rectY, mask);
                            }
                        }
                    }
                }
            } catch (std::exception const& e) { error = e.what(); }
            if (cancelled()) return;
            Loader::get()->queueInMainThread([generation, ticket, index, packed, error]() {
                if (paimon::isRuntimeShuttingDown() || generation->load() != ticket) return;
                auto& runtime = get();
                if (index >= runtime.m_sources.size()) return;
                auto& source = runtime.m_sources[index];
                if (packed && !packed->empty()) {
                    source.width = packed->width();
                    source.height = packed->height();
                    source.mask = SpritePreviewRenderer::createTexture(*packed);
                    if (source.mask) {
                        ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                        source.mask->setTexParameters(&params);
                        runtime.refreshTextures();
                    } else ++runtime.m_failures;
                }
                if (!error.empty()) {
                    ++runtime.m_failures;
                    log::warn("[Pack Gen] {}: {}", source.base, error);
                }
                if (index + 1 == runtime.m_sources.size()) {
                    runtime.m_pending = false;
                    runtime.refreshTextures();
                }
            });
        }
    });
    if (!started) { m_pending = false; m_error = "Analysis could not start"; }
}

CCGLProgram* LiveSlotRuntime::prepareDraw(CCTexture2D* texture, CCGLProgram* original) {
    if (!m_enabled || !texture || paimon::isRuntimeShuttingDown()) return nullptr;
    auto it = m_textures.find(texture);
    if (it == m_textures.end()) return nullptr;
    auto const& source = m_sources[it->second];
    auto size = texture->getContentSizeInPixels();
    if (!source.mask || size.width != source.width || size.height != source.height) return nullptr;
    // custom shaders own uniforms/samplers, icon gradients included.
    if (original != CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor)) return nullptr;
    auto* shader = m_shader.data();
    if (!shader) return nullptr;
    shader->use();
    // some devices pad npot sources; role mask still uses image dims.
    float maskScaleX = static_cast<float>(texture->getPixelsWide()) / source.width;
    float maskScaleY = static_cast<float>(texture->getPixelsHigh()) / source.height;
    if (m_uniformsDirty || m_premultiplied != texture->hasPremultipliedAlpha() ||
        m_maskScaleX != maskScaleX || m_maskScaleY != maskScaleY) {
        auto scalar = [&](char const* name, float v) {
            shader->setUniformLocationWith1f(shader->getUniformLocationForName(name), v);
        };
        auto color = [&](char const* name, ccColor3B c) {
            shader->setUniformLocationWith3f(shader->getUniformLocationForName(name), c.r, c.g, c.b);
        };
        shader->setUniformLocationWith1i(shader->getUniformLocationForName("u_texture"), 0);
        shader->setUniformLocationWith1i(shader->getUniformLocationForName("u_roleMask"), 1);
        color("u_color1", m_project.color1);
        color("u_color2", m_project.color2);
        color("u_detailColor", m_project.colorDetail);
        color("u_glowColor", m_project.colorGlow);
        scalar("u_brightness", m_project.brightness);
        scalar("u_saturation", m_project.saturation);
        scalar("u_contrast", m_project.contrast);
        scalar("u_darkThreshold", m_project.outlineProtect);
        scalar("u_glowReplace", m_project.alternativeGlowOverlay ? 1.f : 0.f);
        scalar("u_applyDetail", m_project.colorDetail.r != 255 || m_project.colorDetail.g != 255 ||
            m_project.colorDetail.b != 255 ? 1.f : 0.f);
        scalar("u_strength", m_project.tintStrength);
        scalar("u_glowStrength", m_project.glowStrength);
        scalar("u_premultiplied", texture->hasPremultipliedAlpha() ? 1.f : 0.f);
        scalar("u_maskScaleX", maskScaleX);
        scalar("u_maskScaleY", maskScaleY);
        m_uniformsDirty = false;
        m_premultiplied = texture->hasPremultipliedAlpha();
        m_maskScaleX = maskScaleX;
        m_maskScaleY = maskScaleY;
    }
    ccGLBindTexture2DN(1, source.mask->getName());
    glActiveTexture(GL_TEXTURE0);
    return shader;
}

void LiveSlotRuntime::onGLContextReload() {
    ++*m_generation;
    m_enabled = false;
    m_restored = false;
    m_pending = false;
    m_sources.clear();
    m_textures.clear();
    m_shader = nullptr;
    m_uniformsDirty = true;
}

std::string LiveSlotRuntime::status() const {
    if (!m_error.empty()) return m_error;
    if (!m_enabled) return "Original colors";
    if (m_pending) return "Analyzing color regions...";
    if (m_failures) return fmt::format("Live - {} textures unavailable", m_failures);
    return "Live colors";
}

}
