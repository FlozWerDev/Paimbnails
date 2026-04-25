#pragma once

#include <Geode/utils/function.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include <utility>

namespace cocos2d {
    class CCTexture2D;
    class CCNode;
}

// Quality settings for capture
struct CaptureQualitySettings {
    int targetWidth = 1920;
    int supersampleFactor = 1; // 1 = no supersampling, 2 = 2x, etc.
    bool useAntialiasing = true;
    bool highQualityFiltering = true;
};

/**
 * Captures the game scene by re-rendering into a high-resolution FBO.
 *
 * Works with shaders via direct back-buffer capture that already contains
 * the final composited frame with all effects applied.
 * Output quality uses maximum settings (single resolution, no tiers).
 */
class FramebufferCapture {
public:
    // Request a capture on the next frame.
    // Callback receives: (success, texture, rgbaData, width, height)
    static void requestCapture(
        int levelID, 
        geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height)> callback,
        cocos2d::CCNode* nodeToCapture = nullptr
    );
    
    // Cancel a pending capture.
    static void cancelPending();
    
    // Called from the CCDirector hook to perform the capture.
    static void executeIfPending();
    
    // Returns whether a capture is pending.
    static bool hasPendingCapture();
    
    // Whether a capture is currently in progress.
    static bool isCapturing();
    
    // Current capture target dimensions (valid only when isCapturing() == true).
    static std::pair<int, int> getCaptureSize();

    // Process deferred callbacks (call after the full frame).
    static void processDeferredCallbacks();
    
    // Query the GPU's maximum texture size (cached after first call).
    static int getMaxTextureSize();

private:
    struct CaptureRequest {
        int levelID;
        geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        cocos2d::CCNode* nodeToCapture = nullptr;
        bool active = false;
    };
    
    struct DeferredCallback {
        geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        bool success;
        cocos2d::CCTexture2D* texture;
        std::shared_ptr<uint8_t> rgbaData;
        int width;
        int height;
    };
    
    // RAII guard that sets isCapturing + captureSize on construction,
    // and clears both on destruction (even if an exception is thrown).
    struct CaptureGuard {
        CaptureGuard(int w, int h);
        ~CaptureGuard();
        CaptureGuard(CaptureGuard const&) = delete;
        CaptureGuard& operator=(CaptureGuard const&) = delete;
    };

    // RAII guard that saves and restores critical GL state around a render pass.
    struct GLStateGuard {
        GLStateGuard();
        ~GLStateGuard();
        GLStateGuard(GLStateGuard const&) = delete;
        GLStateGuard& operator=(GLStateGuard const&) = delete;
    private:
        int m_viewport[4]{};
        int m_fbo = 0;
        bool m_blend = false;
        int m_blendSrc = 0;
        int m_blendDst = 0;
        bool m_scissor = false;
        bool m_depthTest = false;
        bool m_stencilTest = false;
    };

    static CaptureRequest s_request;
    static std::vector<DeferredCallback> s_deferredCallbacks;
    static bool s_isCapturing;
    static int  s_captureW;
    static int  s_captureH;
    static int  s_maxTextureSize;

    // Capture a specific node into a CCRenderTexture.
    static void doCaptureNode(cocos2d::CCNode* node);

    // Re-render the scene into a separate FBO at the target resolution.
    static void doCaptureRerender(int targetWidth, int viewportW, int viewportH, CaptureQualitySettings const& quality);

    // Capture directly from the back-buffer and high-quality downscale.
    static void doCaptureDirectWithScale(int targetWidth, int viewportW, int viewportH, CaptureQualitySettings const& quality);

    // Vertical flip (OpenGL origin is bottom-left).
    static void flipVertical(std::vector<uint8_t>& pixels, int width, int height, int channels);

    // High-quality Lanczos-3 downscale for RGBA buffers.
    // Returns a new buffer of size outW * outH * 4.
    static std::shared_ptr<uint8_t> lanczosDownscale(
        uint8_t const* src, int srcW, int srcH,
        int outW, int outH
    );
};
