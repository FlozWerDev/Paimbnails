#include "LiveWallpaperNode.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include "../services/WallpaperLayout.hpp"
#include "../services/WebGpuSurface.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../video/PBOUploader.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/utils/file.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <unordered_map>
#include <unordered_set>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace paimon::wallpaper {

// draws one browser frame. The shader forces alpha to 1: a captured window's
// alpha channel is not reliable and the wallpaper is always opaque.
class WebFrameQuad : public CCNode {
public:
    static WebFrameQuad* create() {
        auto* node = new (std::nothrow) WebFrameQuad;
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void setTextureName(GLuint name) { m_texture = name; }
    GLuint textureName() const { return m_texture; }

    void draw() override {
        if (!m_texture) return;
        auto* program = opaqueProgram();
        if (!program) return;
        auto const size = getContentSize();
        if (size.width <= 0.f || size.height <= 0.f) return;
        program->use();
        program->setUniformsForBuiltins();
        ccGLBlendFunc(GL_ONE, GL_ZERO);
        ccGLBindTexture2D(m_texture);
#if CC_TEXTURE_ATLAS_USE_VAO
        ccGLBindVAO(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
        GLfloat const positions[] = {0.f, 0.f, size.width, 0.f, 0.f, size.height, size.width, size.height};
        // texture rows run top-down, so the top edge samples v = 0.
        GLfloat const coords[] = {0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f};
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, positions);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, coords);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CC_INCREMENT_GL_DRAWS(1);
    }

private:
    static CCGLProgram* opaqueProgram() {
        static char const* vertex =
            "attribute vec4 a_position;\n"
            "attribute vec2 a_texCoord;\n"
            "#ifdef GL_ES\nvarying mediump vec2 v_texCoord;\n#else\nvarying vec2 v_texCoord;\n#endif\n"
            "void main() { gl_Position = CC_MVPMatrix * a_position; v_texCoord = a_texCoord; }\n";
        static char const* fragment =
            "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
            "varying vec2 v_texCoord;\n"
            "uniform sampler2D u_texture;\n"
            "void main() { gl_FragColor = vec4(texture2D(u_texture, v_texCoord).rgb, 1.0); }\n";
        return paimon::shaders::loadShader("paimon-web-wallpaper-opaque"_spr, "", "", vertex, fragment);
    }

    GLuint m_texture = 0;
};

namespace {
struct RendererEntry {
    std::shared_ptr<WebWallpaperRenderer> renderer;
    std::chrono::steady_clock::time_point idleSince{};
    bool idle = false;
};

// tearing down WebView2 costs a second or more, so a wallpaper that is no
// longer displayed stays alive (suspended, drawing nothing) for a while.
// coming back to the same menu then paints on the first frame.
std::unordered_map<std::string, RendererEntry>& renderers() {
    // Heap-leaked: registry must not destroy WebWallpaperRenderer instances in DllMain.
    static auto* registry = new std::unordered_map<std::string, RendererEntry>();
    return *registry;
}

constexpr auto kIdleKeepAlive = std::chrono::seconds(45);
constexpr size_t kMaxIdleRenderers = 2;

// WebView2 teardown joins the browser thread and can take a second; doing it
// on the game thread froze the menu. At shutdown it must finish before exit.
void retireRenderer(std::shared_ptr<WebWallpaperRenderer> renderer) {
    if (!renderer) return;
    if (!paimon::isRuntimeShuttingDown()) {
        if (paimon::ThreadTracker::get().spawn([renderer]() mutable {
                geode::utils::thread::setName("WebWallpaperStop");
                renderer->stop();
                renderer.reset();
            })) return;
    }
    renderer->stop();
}

void releaseIdleRenderers(bool force) {
    auto& registry = renderers();
    auto const now = std::chrono::steady_clock::now();
    for (auto it = registry.begin(); it != registry.end();) {
        if (it->second.idle && (force || now - it->second.idleSince > kIdleKeepAlive)) {
            if (force) { if (it->second.renderer) it->second.renderer->stop(); }
            else retireRenderer(std::move(it->second.renderer));
            it = registry.erase(it);
        } else {
            ++it;
        }
    }
    for (;;) {
        size_t idleCount = 0;
        auto oldest = registry.end();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (!it->second.idle) continue;
            ++idleCount;
            if (oldest == registry.end() || it->second.idleSince < oldest->second.idleSince) oldest = it;
        }
        if (idleCount <= kMaxIdleRenderers || oldest == registry.end()) break;
        if (force) { if (oldest->second.renderer) oldest->second.renderer->stop(); }
        else retireRenderer(std::move(oldest->second.renderer));
        registry.erase(oldest);
    }
}

// one popup per project and message: a broken wallpaper must not notify on
// every menu visit, the status label already says what failed.
bool firstReport(std::string const& key) {
    static std::unordered_set<std::string> reported;
    return reported.insert(key).second;
}

// PBO and video uploads bind textures with raw GL calls; cocos then still
// believes its cached texture is bound and would skip the next bind.
void resyncTextureBinding() {
    ccGLBindTexture2D(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::unordered_set<LiveWallpaperNode*>& activeNodes() {
    static auto* nodes = new std::unordered_set<LiveWallpaperNode*>();
    return *nodes;
}

// every live instance, on screen or not: gl reloads must reach detached ones.
std::unordered_set<LiveWallpaperNode*>& allNodes() {
    static auto* nodes = new std::unordered_set<LiveWallpaperNode*>();
    return *nodes;
}

// gpu path availability for the current GL context; -1 = not probed yet.
int& gpuPathState() {
    static int state = -1;
    return state;
}

bool gpuPathAvailable() {
    auto& state = gpuPathState();
    if (state < 0) state = WebGpuSurface::supported() ? 1 : 0;
    return state == 1;
}

uint32_t glVendorId() {
    auto const* vendor = reinterpret_cast<char const*>(glGetString(GL_VENDOR));
    if (!vendor) return 0;
    std::string text(vendor);
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text.find("nvidia") != std::string::npos) return 0x10DE;
    if (text.find("amd") != std::string::npos || text.find("ati ") != std::string::npos ||
        text.find("advanced micro") != std::string::npos) return 0x1002;
    if (text.find("intel") != std::string::npos) return 0x8086;
    return 0;
}

bool gameWindowMinimized() {
#ifdef GEODE_IS_WINDOWS
    static HWND window = nullptr;
    if (!window || !IsWindow(window)) window = WindowFromDC(wglGetCurrentDC());
    return window && IsIconic(window);
#else
    return false;
#endif
}
} // namespace

LiveWallpaperNode* LiveWallpaperNode::create(std::filesystem::path const& project,
    CCSize viewport, LayerBgConfig const& config) {
    auto* node = new (std::nothrow) LiveWallpaperNode;
    if (node && node->initProject(project, viewport, config)) { node->autorelease(); return node; }
    CC_SAFE_DELETE(node);
    return nullptr;
}

bool LiveWallpaperNode::initProject(std::filesystem::path const& project, CCSize viewport, LayerBgConfig const& config) {
    if (!CCNode::init() || viewport.width <= 0.f || viewport.height <= 0.f) return false;
    m_project = project;
    m_audioReactive = config.audioReactive;
    m_audioSensitivity = config.audioSensitivity;
    m_fitMode = config.fitMode;
    setContentSize(viewport);
    setAnchorPoint({0.f, 0.f});
    setPosition({0.f, 0.f});
    m_background = CCLayerColor::create({0, 0, 0, 255});
    if (m_background) {
        m_background->setContentSize(viewport);
        addChild(m_background, 0);
    }
    m_quad = WebFrameQuad::create();
    if (!m_quad) return false;
    m_quad->setAnchorPoint({0.5f, 0.5f});
    m_quad->ignoreAnchorPointForPosition(false);
    m_quad->setVisible(false);
    addChild(m_quad, 1);
    m_status = CCLabelBMFont::create("Cargando fondo Web...", "chatFont.fnt");
    if (m_status) {
        m_status->setPosition(viewport / 2.f);
        m_status->setScale(std::min(.6f, viewport.width / 500.f));
        addChild(m_status, 10);
    }
    m_path = gpuPathAvailable() ? Path::Gpu : Path::Cpu;
    allNodes().insert(this);
    scheduleUpdate();
    return true;
}

void LiveWallpaperNode::acquireRenderer() {
    if (m_renderer || paimon::isRuntimeShuttingDown()) return;
    releaseIdleRenderers(false);
    auto& registry = renderers();
    auto const key = paimon::paths::toUtf8(m_project.lexically_normal());
    m_gpuSequence = 0;
    m_sequence = 0;
    m_reportedError = false;
    m_consumerActive = false;
    if (auto it = registry.find(key); it != registry.end() && it->second.renderer) {
        if (it->second.renderer->hasError()) {
            // a failed browser never recovers (its thread already exited):
            // start a fresh one instead of showing the stale error forever.
            bool inUse = false;
            for (auto* node : activeNodes()) {
                if (node != this && node->m_renderer == it->second.renderer) { inUse = true; break; }
            }
            if (!inUse) retireRenderer(std::move(it->second.renderer));
            registry.erase(it);
        } else {
            // a parked renderer for this exact project: no browser startup at all.
            it->second.idle = false;
            m_renderer = it->second.renderer;
            return;
        }
    }
    auto contents = utils::file::readString(paimon::paths::toSdkPath(m_project));
    if (!contents) { reportError("No se encontro el proyecto del fondo."); return; }
    auto project = matjson::parse(contents.unwrap());
    if (!project || !project.unwrap().isObject()) { reportError("El proyecto del fondo no es valido."); return; }
    auto type = project.unwrap()["type"].asString().unwrapOr("");
    for (auto& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (type != "web") { reportError("Este proyecto no es un fondo Web."); return; }
    auto const entry = project.unwrap()["file"].asString().unwrapOr("index.html");
    auto const relative = paimon::assets::pathFromUtf8(entry).lexically_normal();
    if (relative.is_absolute() || relative.empty() || *relative.begin() == "..") {
        reportError("La ruta HTML del proyecto no es valida."); return;
    }
    auto const bridgePath = paimon::paths::toStdPath(Mod::get()->getResourcesDir()) / "bridge.js";
    auto bridge = utils::file::readString(paimon::paths::toSdkPath(bridgePath));
    if (!bridge) { reportError("No se encontro el puente de audio del fondo Web."); return; }
    WebWallpaperOptions options;
    options.root = m_project.parent_path();
    options.browserStorage = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "web-wallpaper-browser";
    options.entryFile = paimon::paths::toUtf8(relative);
    options.bridgeScript = bridge.unwrap();
    options.preferredVendorId = glVendorId();
    // match the display aspect ratio at the real framebuffer width: the gpu
    // path costs nothing extra per pixel, the cpu fallback stays capped.
    auto const viewport = getContentSize();
    float pixelWidth = viewport.width;
    if (auto* view = CCDirector::get()->getOpenGLView()) {
        auto const frameSize = view->getFrameSize();
        if (frameSize.width > 0.f) pixelWidth = frameSize.width;
    }
    int const maxWidth = m_path == Path::Gpu ? 2560 : 1600;
    options.width = std::clamp(static_cast<int>(std::lround(pixelWidth)), 256, maxWidth);
    options.height = std::clamp(
        static_cast<int>(std::lround(options.width * viewport.height / std::max(viewport.width, 1.f))), 64, 1440);
    m_renderer = std::make_shared<WebWallpaperRenderer>(std::move(options));
    registry[key] = RendererEntry{m_renderer, {}, false};
}

void LiveWallpaperNode::parkRenderer() {
    if (!m_renderer) return;
    auto const key = paimon::paths::toUtf8(m_project.lexically_normal());
    if (auto it = renderers().find(key); it != renderers().end() && it->second.renderer == m_renderer) {
        // another node may still display this renderer.
        bool shared = false;
        for (auto* node : activeNodes()) {
            if (node != this && node->m_renderer == m_renderer) { shared = true; break; }
        }
        if (!shared) {
            it->second.idle = true;
            it->second.idleSince = std::chrono::steady_clock::now();
        }
    }
    m_renderer.reset();
}

LiveWallpaperNode::~LiveWallpaperNode() {
    releaseAudio();
    setConsumer(false);
    activeNodes().erase(this);
    allNodes().erase(this);
    parkRenderer();
    if (m_surface) m_surface->release();
}

void LiveWallpaperNode::onEnter() {
    CCNode::onEnter();
    activeNodes().insert(this);
    acquireRenderer();
}

void LiveWallpaperNode::onExit() {
    releaseAudio();
    setConsumer(false);
    activeNodes().erase(this);
    // keep the browser parked rather than paying the startup again on the way
    // back; eviction happens on the next acquire or at shutdown.
    parkRenderer();
    CCNode::onExit();
}

// the capture worker suspends the page when no node draws it.
void LiveWallpaperNode::setConsumer(bool active) {
    auto const kind = m_path == Path::Gpu ? WebConsumer::Gpu : WebConsumer::Cpu;
    if (m_consumerActive == active && (!active || m_consumerKind == kind)) return;
    if (m_renderer && m_consumerActive) m_renderer->removeConsumer(m_consumerKind);
    m_consumerActive = active && m_renderer;
    m_consumerKind = kind;
    if (m_renderer && m_consumerActive) m_renderer->addConsumer(kind);
}

void LiveWallpaperNode::layoutImage() {
    if (!m_quad || m_frameWidth <= 0 || m_frameHeight <= 0) return;
    auto const viewport = getContentSize();
    auto const layout = fitWallpaper(static_cast<float>(m_frameWidth), static_cast<float>(m_frameHeight),
        viewport.width, viewport.height, m_fitMode);
    m_quad->setContentSize({layout.width, layout.height});
    m_quad->setPosition(viewport / 2.f);
}

void LiveWallpaperNode::showFrame(unsigned texture, int width, int height) {
    if (!m_quad) return;
    bool const resized = width != m_frameWidth || height != m_frameHeight;
    m_quad->setTextureName(texture);
    m_quad->setVisible(texture != 0);
    m_frameWidth = width;
    m_frameHeight = height;
    if (resized) layoutImage();
    if (m_status) m_status->setVisible(false);
    // the quad covers the viewport: the black filler only cost a blended quad.
    if (m_background && m_fitMode == "cover") m_background->setVisible(false);
}

void LiveWallpaperNode::fallBackToCpu(char const* reason) {
    log::warn("[WebWallpaper] gpu frame sharing disabled ({}); using cpu readback", reason);
    gpuPathState() = 0;
    if (m_surface) {
        m_surface->release();
        m_surface.reset();
    }
    if (m_quad) m_quad->setTextureName(0);
    bool const wasActive = m_consumerActive;
    setConsumer(false);
    m_path = Path::Cpu;
    m_sequence = 0;
    setConsumer(wasActive);
}

void LiveWallpaperNode::pumpGpu() {
    auto shared = m_renderer->gpuShared();
    if (!shared) return;
    uint64_t const sequence = m_renderer->gpuSequence();
    if (!m_surface) {
        m_surface = std::make_unique<WebGpuSurface>(
            [] { GLuint name = 0; glGenTextures(1, &name); return static_cast<unsigned>(name); },
            [](unsigned name) { ccGLDeleteTexture(static_cast<GLuint>(name)); });
    }
    auto const result = m_surface->update(*shared, sequence != m_gpuSequence);
    if (result == WebGpuSurface::Result::Failed) {
        fallBackToCpu("interop update failed");
        return;
    }
    if (result != WebGpuSurface::Result::Updated) return;
    m_gpuSequence = sequence;
    if (m_surface->textureRecreated()) {
        ccGLBindTexture2D(m_surface->texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    showFrame(m_surface->texture(), m_surface->width(), m_surface->height());
}

void LiveWallpaperNode::pumpCpu() {
    if (m_uploadDisabled) return;
    // without staged uploads the driver syncs on every client-memory handover,
    // so that fallback stays at half rate.
    bool const allowUpload = m_pboUploader != nullptr || (++m_uploadTick % 2u) == 0;
    if (!allowUpload) return;
    std::shared_ptr<WallpaperFrame const> frame;
    if (m_renderer->takeLatestFrame(m_sequence, frame)) uploadFrame(*frame);
}

void LiveWallpaperNode::uploadFrame(WallpaperFrame const& frame) {
    auto* texture = m_cpuTexture.data();
    if (!texture || texture->getPixelsWide() != static_cast<unsigned int>(frame.width) ||
        texture->getPixelsHigh() != static_cast<unsigned int>(frame.height)) {
        auto next = Ref<CCTexture2D>::adopt(new (std::nothrow) CCTexture2D);
        if (!next || !next->initWithData(frame.rgba.data(), kCCTexture2DPixelFormat_RGBA8888,
            frame.width, frame.height, {static_cast<float>(frame.width), static_cast<float>(frame.height)})) {
            dropFailedUpload(frame);
            return;
        }
        ccTexParams parameters{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        next->setTexParameters(&parameters);
        m_cpuTexture = next;
        prepareUploadPath(frame);
    } else if (m_pboUploader && m_pboUploader->uploadRGBA(texture->getName(), frame.rgba.data(), frame.width, frame.height)) {
        resyncTextureBinding();
    } else {
        ccGLBindTexture2D(texture->getName());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
    }
    m_failedUploads = 0;
    m_sequence = frame.sequence;
    showFrame(m_cpuTexture->getName(), frame.width, frame.height);
}

// re-arms the staged upload for the current frame size.
void LiveWallpaperNode::prepareUploadPath(WallpaperFrame const& frame) {
    int const bytes = frame.width * frame.height * 4;
    if (bytes <= 0 || m_pboBytes == bytes) return;
    m_pboBytes = bytes;
    if (m_pboUnavailable) return;
    if (m_pboUploader) m_pboUploader->shutdown();
    else m_pboUploader = std::make_unique<paimon::video::PBOUploader>();
    if (!m_pboUploader->init(bytes)) {
        m_pboUploader.reset();
        m_pboUnavailable = true;
    }
}

// a frame that cannot be uploaded must not be retried forever.
void LiveWallpaperNode::dropFailedUpload(WallpaperFrame const& frame) {
    m_sequence = frame.sequence;
    if (++m_failedUploads >= 3 && !m_uploadDisabled) {
        m_uploadDisabled = true;
        reportError("El fondo Web no pudo crear su textura.");
    }
}

void LiveWallpaperNode::updatePointer() {
    if (!m_quad || !m_quad->isVisible() || !m_renderer) return;
    // a foreground preview owns input over the background sharing its browser.
    auto const mouse = geode::cocos::getMousePos();
    LiveWallpaperNode* owner = nullptr;
    float ownerArea = 0.f;
    for (auto* candidate : activeNodes()) {
        if (candidate->m_renderer != m_renderer || !candidate->visibleInHierarchy()) continue;
        auto const position = candidate->convertToNodeSpace(mouse);
        auto const bounds = candidate->getContentSize();
        if (position.x < 0.f || position.y < 0.f || position.x > bounds.width || position.y > bounds.height) continue;
        float const area = bounds.width * bounds.height;
        if (!owner || area < ownerArea) { owner = candidate; ownerArea = area; }
    }
    if (owner && owner != this) return;
    auto const local = m_quad->convertToNodeSpace(mouse);
    auto const size = m_quad->getContentSize();
    auto const point = convertToNodeSpace(mouse);
    auto const viewport = getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;
    WallpaperPointer pointer;
    pointer.x = local.x / size.width;
    pointer.y = 1.f - local.y / size.height;
    pointer.inside = local.x >= 0.f && local.x <= size.width && local.y >= 0.f && local.y <= size.height &&
        point.x >= 0.f && point.x <= viewport.width && point.y >= 0.f && point.y <= viewport.height;
#ifdef GEODE_IS_WINDOWS
    if (pointer.inside) {
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        pointer.inside = foregroundProcess == GetCurrentProcessId();
        if (pointer.inside) {
            pointer.left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            pointer.right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            pointer.middle = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        }
    }
#endif
    // only ship real state changes: an idle mouse must not enqueue anything.
    if (m_hasLastPointer) {
        bool const moved = std::abs(pointer.x - m_lastPointer.x) > 0.0015f ||
            std::abs(pointer.y - m_lastPointer.y) > 0.0015f;
        if (!moved && pointer.inside == m_lastPointer.inside &&
            pointer.left == m_lastPointer.left && pointer.right == m_lastPointer.right &&
            pointer.middle == m_lastPointer.middle) return;
    }
    m_hasLastPointer = true;
    m_lastPointer = pointer;
    m_renderer->setPointer(pointer);
}

void LiveWallpaperNode::releaseAudio() {
    // detach from the channel the dsp was attached to: gd swaps
    // m_backgroundMusicChannel between songs and the old one may be gone.
    auto* attached = m_fftChannel;
    m_fftChannel = nullptr;
    if (!m_fft) return;
    if (attached) attached->removeDSP(m_fft);
    m_fft->release();
    m_fft = nullptr;
}

void LiveWallpaperNode::updateAudio() {
    std::array<float, 128> spectrum{};
    auto* engine = FMODAudioEngine::sharedEngine();
    if (m_audioReactive && engine && engine->m_system) {
        auto* musicChannel = engine->m_backgroundMusicChannel;
        if (musicChannel != m_fftChannel) {
            releaseAudio();
            if (musicChannel && engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fft) == FMOD_OK && m_fft) {
                m_fft->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, 512);
                if (musicChannel->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, m_fft) == FMOD_OK) m_fftChannel = musicChannel;
                else releaseAudio();
            }
        }
        FMOD_DSP_PARAMETER_FFT* fft = nullptr;
        if (m_fft && m_fft->getParameterData(FMOD_DSP_FFT_SPECTRUMDATA, reinterpret_cast<void**>(&fft), nullptr, nullptr, 0) == FMOD_OK &&
            fft && fft->numchannels > 0 && fft->length > 0) {
            // wallpaper Engine hands out 64 bands per channel on a roughly
            // logarithmic scale; average the fft bins that fall in each band.
            int const bins = fft->length;
            for (size_t channel = 0; channel < 2; ++channel) {
                auto const* data = fft->spectrum[std::min(static_cast<int>(channel), fft->numchannels - 1)];
                if (!data) continue;
                for (size_t band = 0; band < 64; ++band) {
                    float const lo = std::pow(static_cast<float>(bins) * 0.5f, band / 64.f);
                    float const hi = std::pow(static_cast<float>(bins) * 0.5f, (band + 1) / 64.f);
                    int const first = std::clamp(static_cast<int>(lo), 0, bins - 1);
                    int const last = std::clamp(std::max(first + 1, static_cast<int>(hi)), first + 1, bins);
                    float sum = 0.f;
                    for (int bin = first; bin < last; ++bin) sum += data[bin];
                    float const value = sum / static_cast<float>(last - first);
                    spectrum[channel * 64 + band] = std::clamp(value * 6.f * m_audioSensitivity, 0.f, 1.f);
                }
            }
        }
    }
    // ship only real changes.
    for (size_t i = 0; i < spectrum.size(); ++i) {
        if (std::abs(spectrum[i] - m_lastSpectrum[i]) > 0.0015f) {
            m_lastSpectrum = spectrum;
            m_renderer->setAudio(spectrum);
            return;
        }
    }
}

void LiveWallpaperNode::reportError(std::string const& message) {
    if (m_reportedError) return;
    m_reportedError = true;
    if (m_status) { m_status->setString(message.c_str()); m_status->setVisible(true); }
    if (m_background) m_background->setVisible(true);
    log::warn("[WebWallpaper] {}", message);
    if (firstReport(paimon::paths::toUtf8(m_project) + "|" + message)) {
        PaimonNotify::show(message, NotificationIcon::Error, 5.f);
    }
}

bool LiveWallpaperNode::visibleInHierarchy() const {
    return isNodeVisibleInHierarchy(this);
}

void LiveWallpaperNode::updateConfig(LayerBgConfig const& config) {
    bool const audioWasReactive = m_audioReactive;
    m_audioReactive = config.audioReactive;
    m_audioSensitivity = config.audioSensitivity;
    if (m_fitMode != config.fitMode) {
        m_fitMode = config.fitMode;
        m_layoutDirty = true;
        if (m_background) m_background->setVisible(m_fitMode != "cover" || !m_quad || !m_quad->isVisible());
    }
    if (!m_audioReactive) releaseAudio();
    if (audioWasReactive && !m_audioReactive) {
        std::array<float, 128> zeros{};
        m_lastSpectrum = zeros;
        if (m_renderer) m_renderer->setAudio(zeros);
    }
}

void LiveWallpaperNode::invalidateTextures() {
    // the old gl context is still alive: drop interop registrations and
    // textures now so nothing references dead names afterwards.
    if (m_surface) {
        m_surface->release();
        m_surface.reset();
    }
    m_cpuTexture = nullptr;
    if (m_pboUploader) {
        m_pboUploader->shutdown();
        m_pboUploader.reset();
    }
    if (m_quad) {
        m_quad->setTextureName(0);
        m_quad->setVisible(false);
    }
    m_pboBytes = 0;
    m_sequence = 0;
    m_gpuSequence = 0;
    m_failedUploads = 0;
    m_uploadDisabled = false;
    if (m_background) m_background->setVisible(true);
    if (m_status) m_status->setVisible(true);
}

void LiveWallpaperNode::onGLContextReload() {
    for (auto* node : allNodes()) node->invalidateTextures();
    // the next context may expose different extensions.
    gpuPathState() = -1;
}

void LiveWallpaperNode::update(float dt) {
    if (!m_renderer || paimon::isRuntimeShuttingDown()) return;
    // hidden or minimized: no uploads and no input; with no consumer left the
    // worker suspends the page instead of rendering frames nobody sees.
    bool const visible = visibleInHierarchy() && !gameWindowMinimized();
    setConsumer(visible);
    if (!visible) return;
    if (m_layoutDirty) {
        m_layoutDirty = false;
        layoutImage();
    }
    if (m_path == Path::Gpu) pumpGpu();
    else pumpCpu();

    if (std::isfinite(dt) && dt > 0.f) {
        m_pointerElapsed += dt;
        m_audioElapsed += dt;
    }
    constexpr float kPointerInterval = 1.f / 60.f;
    if (m_pointerElapsed >= kPointerInterval) {
        m_pointerElapsed = std::fmod(m_pointerElapsed, kPointerInterval);
        updatePointer();
    }
    if (m_audioReactive) {
        constexpr float kAudioInterval = 1.f / 30.f;
        if (m_audioElapsed >= kAudioInterval) {
            m_audioElapsed = std::fmod(m_audioElapsed, kAudioInterval);
            updateAudio();
        }
    }
    if (m_renderer->hasError()) {
        auto const error = m_renderer->error();
        if (!error.empty()) reportError(error);
    }
}

void LiveWallpaperNode::requestStopAll() {
    for (auto* node : activeNodes()) node->releaseAudio();
    for (auto const& [key, entry] : renderers()) {
        if (entry.renderer) entry.renderer->requestStop();
    }
}

void LiveWallpaperNode::finishShutdown(std::chrono::steady_clock::time_point deadline) {
    requestStopAll();
    for (auto const& [key, entry] : renderers()) {
        if (entry.renderer) entry.renderer->waitStop(deadline);
    }
    renderers().clear();
    activeNodes().clear();
    allNodes().clear();
}

void LiveWallpaperNode::shutdown() {
    finishShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

} // namespace paimon::wallpaper
