#pragma once

#include <Geode/Geode.hpp>
#include <fmod.hpp>
#include <chrono>
#include "../services/WebWallpaperRenderer.hpp"

struct LayerBgConfig;

namespace paimon::video { class PBOUploader; }

namespace paimon::wallpaper {

class WebGpuSurface;
class WebFrameQuad;

// displays an embedded-browser wallpaper. Frames normally stay on the gpu
// (D3D11 -> GL interop); drivers without WGL_NV_DX_interop2 fall back to a
// rate-limited cpu readback uploaded through PBOs.
// its viewport stays in Cocos points. Only the child texture changes resolution.
class LiveWallpaperNode : public cocos2d::CCNode {
public:
    static LiveWallpaperNode* create(std::filesystem::path const& project,
        cocos2d::CCSize viewport, LayerBgConfig const& config);
    ~LiveWallpaperNode() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    static void requestStopAll();
    static void finishShutdown(std::chrono::steady_clock::time_point deadline);
    static void shutdown();
    // re-apply cheap settings (fit/audio) without tearing down the embedded
    // browser; a full rebuild restarts a multi-second WebView2.
    void updateConfig(LayerBgConfig const& config);
    // release gl-owned textures while the old context is still alive.
    static void onGLContextReload();

private:
    enum class Path { Gpu, Cpu };

    bool initProject(std::filesystem::path const& project, cocos2d::CCSize viewport, LayerBgConfig const& config);
    void acquireRenderer();
    void parkRenderer();
    void setConsumer(bool active);
    void pumpGpu();
    void pumpCpu();
    void fallBackToCpu(char const* reason);
    void showFrame(unsigned texture, int width, int height);
    void uploadFrame(WallpaperFrame const& frame);
    void prepareUploadPath(WallpaperFrame const& frame);
    void dropFailedUpload(WallpaperFrame const& frame);
    void layoutImage();
    void updatePointer();
    void updateAudio();
    void releaseAudio();
    void invalidateTextures();
    bool visibleInHierarchy() const;
    void reportError(std::string const& message);

    std::filesystem::path m_project;
    std::shared_ptr<WebWallpaperRenderer> m_renderer;
    Path m_path = Path::Cpu;
    WebConsumer m_consumerKind = WebConsumer::Cpu;
    bool m_consumerActive = false;
    bool m_reportedError = false;
    bool m_audioReactive = true;
    float m_audioSensitivity = 1.f;
    float m_pointerElapsed = 1.f / 30.f;
    float m_audioElapsed = 0.f;
    std::string m_fitMode = "cover";

    WebFrameQuad* m_quad = nullptr;
    cocos2d::CCLayerColor* m_background = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    int m_frameWidth = 0;
    int m_frameHeight = 0;

    // gpu path
    std::unique_ptr<WebGpuSurface> m_surface;
    uint64_t m_gpuSequence = 0;

    // cpu fallback
    geode::Ref<cocos2d::CCTexture2D> m_cpuTexture;
    uint64_t m_sequence = 0;
    std::unique_ptr<paimon::video::PBOUploader> m_pboUploader;
    int m_pboBytes = 0;
    bool m_pboUnavailable = false;
    int m_failedUploads = 0;
    bool m_uploadDisabled = false;
    unsigned m_uploadTick = 0;

    FMOD::DSP* m_fft = nullptr;
    FMOD::ChannelGroup* m_fftChannel = nullptr;
    WallpaperPointer m_lastPointer{};
    bool m_hasLastPointer = false;
    std::array<float, 128> m_lastSpectrum{};
    bool m_layoutDirty = false;
};

} // namespace paimon::wallpaper
