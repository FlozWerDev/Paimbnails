#pragma once

#include "../VideoDecoder.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <codecapi.h>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "mfuuid.lib")
#include <strmif.h>

namespace paimon {

class DecoderMF final : public IVideoDecoder {
public:
    DecoderMF() = default;
    ~DecoderMF() override { closeInternal(); }

    bool open(const std::string& path) override;
    void startDecoding() override;
    void stopDecoding() override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    // Coded size; getWidth/Height return downscaled output.
    int getNativeWidth() const override;
    int getNativeHeight() const override;
    VideoColorMatrix getColorMatrix() const override { return m_colorMatrix; }
    bool isFullRange() const override { return m_fullRange; }
    int getRotationDegrees() const override { return m_rotation; }
    bool isFinished() const override;
    double peekNextPTS() const override;
    double peekSecondPTS() const override;
    const Frame* peekFrame() override;
    void releaseFrame() override;
    bool setLooping(bool loop) override {
        m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    void decodeLoop();
    void closeInternal();
    bool setupD3D11();
    bool setupReader(const std::string& path);
    bool setOutputFormat();
    void refreshLinearStride();
    // False drops the frame.
    bool copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot, size_t bufferSize = 0);
    bool copyPlanesToSlotLinear(BYTE* data, DWORD bufLen, Frame& slot);
    bool createStagingTexture();
    bool copyPlanesFromD3D11(ID3D11Texture2D* srcTexture, UINT subresource, Frame& slot);
    bool fallbackToSoftwareDecode(const std::string& path);
    void downscalePlanes(const Frame& src, Frame& dst, int factor);
    IMFSourceReader*   m_reader     = nullptr;
    IMFDXGIDeviceManager* m_dxgiMgr = nullptr;
    ID3D11Device*      m_d3dDevice  = nullptr;
    ID3D11DeviceContext* m_d3dCtx   = nullptr;
    ID3D11Texture2D*   m_stagingTex = nullptr;
    DXGI_FORMAT         m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    UINT                m_stagingWidth  = 0;
    UINT                m_stagingHeight = 0;
    bool               m_dxvaEnabled = false;
    int                m_dxvaReadbackFailures = 0;
    UINT               m_resetToken = 0;
    // Shared: release via releaseSharedD3D11(), never Release().
    bool               m_sharedD3D = false;
    std::mutex         m_d3dCtxMutex;  // serialises context ops vs DXVA decode (AMD fix)

    VideoRingBuffer    m_ring;
    std::string        m_videoPath;
    int                m_width  = 0;
    int                m_height = 0;
    // Post-downscale output; native when factor == 1.
    int                m_outWidth  = 0;
    int                m_outHeight = 0;
    int                m_downscaleFactor = 1;
    // MF padded stride (e.g. 854 -> 856).
    int                m_linearStride = 0;
    VideoColorMatrix   m_colorMatrix = VideoColorMatrix::Auto;
    bool               m_fullRange = false;
    int                m_rotation = 0;
    // Native-size scratch, downscale only.
    Frame              m_scratch;
    double             m_duration = 0.0;
    GUID               m_pixelFormat = GUID_NULL;

    std::atomic<bool>  m_decoding{false};
    std::atomic<bool>  m_finished{false};
    std::atomic<bool>  m_looping{false};
    std::thread        m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_FOUNDATION
