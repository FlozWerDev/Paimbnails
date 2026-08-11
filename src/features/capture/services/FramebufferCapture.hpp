#pragma once

#include <Geode/utils/function.hpp>
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
    // Callback: (success, texture, rgbaData, width, height).
    // Texture is autoreleased; keepers must retain it (geode::Ref<>).
    static void requestCapture(
        int levelID,
        geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height)> callback,
        cocos2d::CCNode* nodeToCapture = nullptr,
        bool hidePlayer1 = false,
        bool hidePlayer2 = false
    );

    static void cancelPending();

    // Called from CCEGLView pre-swap hook to drive capture state machine.
    static void executeIfPending();

    static bool hasPendingCapture();
    static bool isCapturing();
    static std::pair<int, int> getCaptureSize();

    // Call after the full frame.
    static void processDeferredCallbacks();

    static int getMaxTextureSize();

    static void setHDRMode(bool enabled);
    static bool isHDRMode();

    static CaptureValidation validateCaptureConditions();

    // Renders level with the same pipeline as a real capture, restores before
    // returning. Returns autoreleased texture (setFlipY(true) to display).
    // Hiding players mirrors what the accepted capture will look like.
    static cocos2d::CCTexture2D* renderPreviewTexture(
        int width, int height, bool hidePlayer1 = false, bool hidePlayer2 = false);

    // Internal: don't call from outside the capture service.
    static void clearCaptureFlags();

private:
    struct CaptureRequest {
        int levelID;
        geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        cocos2d::CCNode* nodeToCapture = nullptr;
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
    static std::vector<DeferredCallback> s_deferredCallbacks;
    static bool s_isCapturing;
    static int  s_captureW;
    static int  s_captureH;
    static int  s_maxTextureSize;
    static bool s_hdrMode;

    static void doCaptureNode(cocos2d::CCNode* node);

    static void dispatchProcessing(std::shared_ptr<std::vector<uint8_t>> rawPixels, int width, int height);
};
