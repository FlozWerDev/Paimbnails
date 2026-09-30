#pragma once

#include <Geode/utils/function.hpp>
#include <Geode/utils/cocos.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include <utility>
#include <string>

namespace cocos2d {
    class CCTexture2D;
    class CCNode;
}

struct CaptureValidation {
    bool canCapture = true;
    std::string reason;
};

class FramebufferCapture {
public:
    // callback (success, texture, rgba, w, h); texture +1 released on return,
    // retain to keep. may run off-thread: marshal to main for ui.
    static void requestCapture(
        int levelID,
        geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height)> callback,
        cocos2d::CCNode* nodeToCapture = nullptr,
        bool hidePlayer1 = false,
        bool hidePlayer2 = false
    );

    static void cancelPending();

    // called from cceglview pre-swap hook to drive capture state machine.
    static void executeIfPending();

    static bool hasPendingCapture();
    static bool isCapturing();

    // call after the full frame.
    static void processDeferredCallbacks();

    static int getMaxTextureSize();

    static void setHDRMode(bool enabled);
    static bool isHDRMode();

    static CaptureValidation validateCaptureConditions();

    // same pipeline as a real capture; autoreleased texture, player hiding mirrors the accepted shot.
    static cocos2d::CCTexture2D* renderPreviewTexture(
        int width, int height, bool hidePlayer1 = false, bool hidePlayer2 = false);

    // internal: don't call from outside the capture service.
    static void clearCaptureFlags();

private:
    struct CaptureRequest {
        int levelID;
        geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        // keep a one-frame ownership token so another mod cannot remove and
        // destroy the requested node between the ui event and pre-swap.
        geode::Ref<cocos2d::CCNode> nodeToCapture;
        bool hidePlayer1 = false;
        bool hidePlayer2 = false;
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

    static CaptureRequest s_request;
    // kept outside s_request while cpu processing is in flight so cancellation
    // can still complete the caller exactly once.
    static geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)>
        s_processingCallback;
    // supersede detection uses the g_generation counter captured in the worker
    // lambda; no separate generation member is kept.
    static std::vector<DeferredCallback> s_deferredCallbacks;
    static bool s_isCapturing;
    static int  s_captureW;
    static int  s_captureH;
    static int  s_maxTextureSize;
    static bool s_hdrMode;

    static void doCaptureNode(cocos2d::CCNode* node);
    static void finishPendingFailure();

    static void dispatchProcessing(std::shared_ptr<std::vector<uint8_t>> rawPixels, int width, int height);
};
