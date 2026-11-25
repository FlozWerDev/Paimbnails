#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace paimon::wallpaper {

struct WallpaperFrame {
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
    std::vector<uint8_t> rgba;
};

struct WallpaperPointer {
    float x = 0.5f;
    float y = 0.5f;
    bool inside = false;
    bool left = false;
    bool right = false;
    bool middle = false;
};

struct WebWallpaperOptions {
    std::filesystem::path root;
    std::filesystem::path browserStorage;
    std::string entryFile;
    std::string bridgeScript;
    int width = 1280;
    int height = 720;
    // PCI vendor of the adapter running the game's GL context (0 = default).
    // capturing on the same adapter keeps the zero-copy path on hybrid laptops.
    uint32_t preferredVendorId = 0;
};

// windows-only: device + latest composed frame shared with the GL thread.
// defined in WebGpuShared.hpp.
struct WebGpuShared;

enum class WebConsumer { Cpu, Gpu };

// owns an embedded browser (WebView2), captures its window with Windows
// graphics Capture and publishes frames two ways:
//  - Gpu: the frame stays in a D3D11 texture the GL thread maps through
//    WGL_NV_DX_interop2 (no readback, no conversion, no upload).
//  - Cpu: fallback readback into RGBA buffers for drivers without interop.
// no Wallpaper Engine process or installation is used.
class WebWallpaperRenderer {
public:
    explicit WebWallpaperRenderer(WebWallpaperOptions options);
    ~WebWallpaperRenderer();
    WebWallpaperRenderer(WebWallpaperRenderer const&) = delete;
    WebWallpaperRenderer& operator=(WebWallpaperRenderer const&) = delete;

    // zero-copy: shares the worker's frame buffer; it is never rewritten while
    // the caller holds the shared_ptr.
    bool takeLatestFrame(uint64_t afterSequence, std::shared_ptr<WallpaperFrame const>& out) const;
    // copies the latest cpu frame (offline tools and tests).
    bool copyLatestFrame(uint64_t afterSequence, WallpaperFrame& destination) const;

    // bumps once per composed frame copied into the shared texture.
    uint64_t gpuSequence() const;
    // null until the first frame landed (or off Windows).
    std::shared_ptr<WebGpuShared const> gpuShared() const;

    // cheap atomic probe; only call error() once this returns true.
    bool hasError() const;
    std::string error() const;
    void requestStop();
    void waitStop(std::chrono::steady_clock::time_point deadline);
    void stop();
    void setPointer(WallpaperPointer pointer);
    void setAudio(std::array<float, 128> const& spectrum);
    // while no live node displays this renderer the page is suspended and the
    // worker stops copying frames until a consumer comes back.
    void addConsumer(WebConsumer kind);
    void removeConsumer(WebConsumer kind);
    static bool isAvailable();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace paimon::wallpaper
