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
    bool stopDecoding() override;
    bool consumeFrame(Frame& outFrame) override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    bool isFinished() const override;
    double peekNextPTS() const override;

private:
    void decodeLoop(uint64_t gen);
    void closeInternal();
    bool setupD3D11();
    bool setupReader(const std::string& path);
    bool setOutputFormat();
    void copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot);
    void copyPlanesToSlotLinear(BYTE* data, DWORD bufLen, Frame& slot);
    bool createStagingTexture();
    bool copyPlanesFromD3D11(ID3D11Texture2D* srcTexture, UINT subresource, Frame& slot);
    bool fallbackToSoftwareDecode(const std::string& path);
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

    VideoRingBuffer    m_ring;
    std::string        m_videoPath;
    int                m_width  = 0;
    int                m_height = 0;
    double             m_duration = 0.0;
    GUID               m_subType  = GUID_NULL;
    GUID               m_pixelFormat = GUID_NULL;

    std::atomic<bool>  m_decoding{false};
    std::atomic<bool>  m_finished{false};
    std::atomic<uint64_t> m_generation{0};
    // Set to true when the decode thread starts, false when it exits.
    // Used to ensure closeInternal() doesn't destroy COM/D3D resources while
    // a detached thread is still accessing them.
    std::atomic<bool>  m_threadRunning{false};
    std::thread        m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_FOUNDATION
