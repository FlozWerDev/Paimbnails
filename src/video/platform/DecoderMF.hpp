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
    bool consumeFrame(Frame& outFrame) override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    bool isFinished() const override;
    double peekNextPTS() const override;
    double peekSecondPTS() const override;
    const Frame* peekFrame() override;
    void releaseFrame() override;
    bool isTerminal() const override { return m_decodeThreadDetached.load(std::memory_order_acquire); }

private:
    void decodeLoop();
    void closeInternal();
    bool setupD3D11();
    bool setupReader(const std::string& path);
    bool setOutputFormat();
    void copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot, size_t bufferSize = 0);
    void copyPlanesToSlotLinear(BYTE* data, DWORD bufLen, Frame& slot);
    bool createStagingTexture();
    bool copyPlanesFromD3D11(ID3D11Texture2D* srcTexture, UINT subresource, Frame& slot);
    bool fallbackToSoftwareDecode(const std::string& path);
    // Box-average downscale of the native scratch frame into a smaller ring slot.
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
    /// True when m_d3dDevice / m_d3dCtx point to the process-wide shared
    /// device (created via acquireSharedD3D11()).  In that case we MUST
    /// NOT call Release() on those pointers in closeInternal() — only
    /// drop our ref via releaseSharedD3D11().
    bool               m_sharedD3D = false;
    std::mutex         m_d3dCtxMutex;  // serialises context ops vs DXVA decode (AMD fix)

    VideoRingBuffer    m_ring;
    std::string        m_videoPath;
    int                m_width  = 0;
    int                m_height = 0;
    // Output (post-downscale) dimensions reported via getWidth/getHeight and
    // used to size the ring + every downstream GPU buffer. Equal to m_width/
    // m_height when m_downscaleFactor == 1.
    int                m_outWidth  = 0;
    int                m_outHeight = 0;
    int                m_downscaleFactor = 1;
    // Native-sized scratch frame; only allocated when downscaling is active.
    Frame              m_scratch;
    double             m_duration = 0.0;
    GUID               m_subType  = GUID_NULL;
    GUID               m_pixelFormat = GUID_NULL;

    std::atomic<bool>  m_decoding{false};
    std::atomic<bool>  m_finished{false};
    /// Set to true if timedJoin() detached the decode thread due to a timeout.
    /// When this flag is set, closeInternal() must NOT call Release() on any
    /// COM/D3D object the thread may still be accessing — null out pointers
    /// instead and let the OS reclaim them at process exit.
    std::atomic<bool>  m_decodeThreadDetached{false};
    std::thread        m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_FOUNDATION
