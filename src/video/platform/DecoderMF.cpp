#include "DecoderMF.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <objbase.h>   // CoInitializeEx / CoUninitialize
#include "../../utils/TimedJoin.hpp"
#include "../../core/Settings.hpp"

#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64)
#include <emmintrin.h>
#define PAIMON_DECMF_HAVE_SSE2 1
#endif

namespace paimon {

// SSE2-optimized NV12 deinterleave: 16 Cb + 16 Cr per iteration.
// Falls back to scalar tail. ~6-8x faster than per-byte indexing
// on 1080p (~3 ms saved per frame on the decode thread).
static inline void deinterleaveNV12Row(const uint8_t* uv,
                                       uint8_t* cb, uint8_t* cr, int uvW) {
#if PAIMON_DECMF_HAVE_SSE2
    int c = 0;
    int vecEnd = uvW & ~15;  // 16-pixel blocks
    const __m128i mask = _mm_set1_epi16(0x00FF);
    for (; c < vecEnd; c += 16) {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(uv + c * 2));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(uv + c * 2 + 16));
        // Cb: even bytes — mask low byte of each 16-bit lane, pack
        __m128i cb0 = _mm_and_si128(v0, mask);
        __m128i cb1 = _mm_and_si128(v1, mask);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(cb + c),
                         _mm_packus_epi16(cb0, cb1));
        // Cr: odd bytes — shift right by 8, pack
        __m128i cr0 = _mm_srli_epi16(v0, 8);
        __m128i cr1 = _mm_srli_epi16(v1, 8);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(cr + c),
                         _mm_packus_epi16(cr0, cr1));
    }
    for (; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#else
    for (int c = 0; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#endif
}

namespace {
// Global mutex that serialises D3D11 device creation / destruction.
// Creating or destroying multiple D3D11 hardware devices concurrently
// deadlocks some GPU drivers.  We now keep a single process-wide D3D11
// device that all decoders share — this avoids the 30–150 ms hardware
// device-creation cost on every new VideoPlayer (which is what causes
// the visible 360→120 fps drop when entering a profile with a video).
// Per-decoder state (DXGI manager, source reader, staging texture)
// remains private; only the device + immediate context + Multithread
// guard are shared.
std::mutex g_d3d11Mutex;
ID3D11Device*        g_sharedD3DDevice = nullptr;
ID3D11DeviceContext* g_sharedD3DCtx    = nullptr;
int                  g_sharedD3DRefs   = 0;     // active decoders holding the device
bool                 g_sharedD3DBroken = false; // device-lost / create-failed sticky flag

// Acquire the shared D3D11 device, creating it on first use.  Returns
// false if device creation has previously failed (we don't retry — the
// software fallback path is good enough and retrying can cause repeated
// driver hangs on broken systems).
bool acquireSharedD3D11(ID3D11Device*& outDevice, ID3D11DeviceContext*& outCtx) {
    std::lock_guard lk(g_d3d11Mutex);
    if (g_sharedD3DBroken) return false;

    if (g_sharedD3DDevice && g_sharedD3DCtx) {
        outDevice = g_sharedD3DDevice;
        outCtx    = g_sharedD3DCtx;
        ++g_sharedD3DRefs;
        return true;
    }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL outLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,  // required for DXVA; implies multithread
        levels, 2, D3D11_SDK_VERSION,
        &g_sharedD3DDevice, &outLevel, &g_sharedD3DCtx);

    if (FAILED(hr) || !g_sharedD3DDevice) {
        geode::log::warn("DecoderMF: shared D3D11CreateDevice failed (hr={:08X})", static_cast<unsigned>(hr));
        g_sharedD3DDevice = nullptr;
        g_sharedD3DCtx    = nullptr;
        g_sharedD3DBroken = true;
        return false;
    }

    // Enable driver-level multithread protection on the shared context.
    // Without this, two concurrent decoders racing inside the driver
    // can null-deref AMD's atidxx64.dll.  This is required because the
    // DXGI device manager hands out the same context to every decoder.
    {
        ID3D10Multithread* mt = nullptr;
        hr = g_sharedD3DCtx->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&mt));
        if (SUCCEEDED(hr) && mt) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
            geode::log::info("DecoderMF: shared D3D11 multithread protection enabled");
        } else {
            geode::log::warn("DecoderMF: failed to enable shared D3D11 multithread protection");
        }
    }

    outDevice = g_sharedD3DDevice;
    outCtx    = g_sharedD3DCtx;
    ++g_sharedD3DRefs;
    geode::log::info("DecoderMF: created shared D3D11 device (process-wide, DXVA-capable)");
    return true;
}

// Drop a reference to the shared D3D11 device.  We keep it alive once
// created until the process exits — repeated create/destroy was the
// original lag source, so reuse for the lifetime of the process is the
// goal.  Both arguments are zeroed by the caller; the function exists
// only to balance acquire's ref-count for diagnostic purposes.
void releaseSharedD3D11() {
    std::lock_guard lk(g_d3d11Mutex);
    if (g_sharedD3DRefs > 0) --g_sharedD3DRefs;
    // Intentionally do not destroy the device here — see comment above.
}

void releaseMfObjectsSafely(ID3D11Texture2D*& stagingTex, IMFSourceReader*& reader) {
    __try {
        if (stagingTex) {
            stagingTex->Release();
            stagingTex = nullptr;
        }
        if (reader) {
            reader->Release();
            reader = nullptr;
        }
    } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
               ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        // MF DLLs already unloaded — just null out pointers, process is exiting
        stagingTex = nullptr;
        reader = nullptr;
    }
}

void releaseD3D11Safely(ID3D11Device*& dev, ID3D11DeviceContext*& ctx, IMFDXGIDeviceManager*& mgr) {
    __try {
        if (mgr) { mgr->Release(); mgr = nullptr; }
        if (ctx) { ctx->Release(); ctx = nullptr; }
        if (dev) { dev->Release(); dev = nullptr; }
    } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
               ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        dev = nullptr;
        ctx = nullptr;
        mgr = nullptr;
    }
}
} // namespace

bool DecoderMF::open(const std::string& path) {
    closeInternal();
    m_decodeThreadDetached.store(false, std::memory_order_release);
    m_videoPath = path;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: MFStartup failed (hr={})", hr);
        return false;
    }

    if (!setupD3D11()) {
        geode::log::warn("DecoderMF: D3D11 setup failed, continuing without HW accel");
    }

    if (!setupReader(path)) {
        closeInternal();
        return false;
    }

    if (!m_ring.init(m_outWidth, m_outHeight)) {
        closeInternal();
        return false;
    }

    if (m_downscaleFactor > 1) {
        int uvW = (m_width + 1) / 2;
        int uvH = (m_height + 1) / 2;
        m_scratch.planeY  = Frame::allocAligned(Frame::alignedSize(m_width, m_height));
        m_scratch.planeCb = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
        m_scratch.planeCr = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
        m_scratch.strideY  = Frame::alignedStride(m_width);
        m_scratch.strideCb = Frame::alignedStride(uvW);
        m_scratch.strideCr = Frame::alignedStride(uvW);
        m_scratch.width  = m_width;
        m_scratch.height = m_height;
        if (!m_scratch.planeY || !m_scratch.planeCb || !m_scratch.planeCr) {
            geode::log::warn("DecoderMF: scratch alloc failed, disabling downscale");
            closeInternal();
            return false;
        }
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

bool DecoderMF::setupD3D11() {
    // Use a process-wide shared D3D11 device.  Creating a fresh hardware
    // device on every VideoPlayer used to add 30–150 ms of main-thread work
    // per video (D3D11CreateDevice with VIDEO_SUPPORT spins up the GPU
    // driver, allocates command queues, etc.).  Sharing the device makes
    // the second-and-onwards videos virtually free at the D3D11 layer.
    if (!acquireSharedD3D11(m_d3dDevice, m_d3dCtx)) {
        return false;
    }

    HRESULT hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_dxgiMgr);
    if (FAILED(hr) || !m_dxgiMgr) {
        geode::log::warn("DecoderMF: MFCreateDXGIDeviceManager failed (hr={:08X})", static_cast<unsigned>(hr));
        // We don't own the shared device; drop our ref but don't release it.
        releaseSharedD3D11();
        m_d3dDevice = nullptr;
        m_d3dCtx    = nullptr;
        return false;
    }

    hr = m_dxgiMgr->ResetDevice(m_d3dDevice, m_resetToken);
    m_dxvaEnabled = SUCCEEDED(hr);
    if (!m_dxvaEnabled) {
        geode::log::warn("DecoderMF: DXGI manager ResetDevice failed, DXVA unavailable");
    }

    // Mark that this decoder is using the shared device — closeInternal()
    // must NOT call Release() on it, only drop our ref.  We use the
    // m_sharedD3D flag (added below) to differentiate the codepaths.
    m_sharedD3D = true;
    return true;
}

bool DecoderMF::setupReader(const std::string& path) {
    // Create source reader — enable DXVA if D3D11 device is available.
    // When DXVA is active, decoded frames arrive as D3D11 surfaces;
    // we copy them to a staging texture for CPU readback.
    // If DXVA is unavailable, MF falls back to software decode automatically.
    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) return false;

    // Enable low-latency mode for reduced ReadSample delay
    hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: failed to set MF_LOW_LATENCY");
    }

    // Enable DXVA hardware acceleration for 4K+ decode performance
    // When DXVA is active, MF may output D3D11 texture surfaces instead of
    // system memory buffers. We handle both paths in the decode loop.
    if (m_dxvaEnabled && m_dxgiMgr) {
        hr = attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_dxgiMgr);
        if (FAILED(hr)) {
            geode::log::warn("DecoderMF: failed to set D3D manager, DXVA disabled");
            m_dxvaEnabled = false;
        } else {
            geode::log::info("DecoderMF: DXVA hardware acceleration enabled");
        }
    }

    // Normalize path: MF requires backslashes, not forward slashes
    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wLen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, nullptr, 0);
    if (wLen <= 0) { attrs->Release(); return false; }
    auto* wPath = new (std::nothrow) wchar_t[wLen];
    if (!wPath) { attrs->Release(); return false; }
    MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wPath, wLen);

    hr = MFCreateSourceReaderFromURL(wPath, attrs, &m_reader);
    delete[] wPath;
    attrs->Release();

    if (FAILED(hr) || !m_reader) {
        geode::log::warn("DecoderMF: MFCreateSourceReaderFromURL failed (hr={})", hr);
        return false;
    }

    // Set output format — try I420 first, then YV12, then NV12
    if (!setOutputFormat()) {
        geode::log::warn("DecoderMF: failed to set any output format");
        return false;
    }

    IMFMediaType* currentType = nullptr;
    hr = m_reader->GetCurrentMediaType(
        static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType);
    if (FAILED(hr)) return false;

    UINT32 w = 0, h = 0;
    hr = MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &w, &h);
    if (FAILED(hr)) { currentType->Release(); return false; }

    // Verify actual output subtype — MF may silently change format
    GUID actualSubtype = GUID_NULL;
    if (SUCCEEDED(currentType->GetGUID(MF_MT_SUBTYPE, &actualSubtype))) {
        if (actualSubtype != m_pixelFormat) {
            const char* actualName =
                actualSubtype == MFVideoFormat_NV12 ? "NV12" :
                actualSubtype == MFVideoFormat_I420 ? "I420" :
                actualSubtype == MFVideoFormat_YV12 ? "YV12" : "unknown";
            geode::log::warn("DecoderMF: actual output subtype ({}) differs from requested", actualName);
            m_pixelFormat = actualSubtype;
        }
    }
    currentType->Release();

    m_width  = static_cast<int>(w);
    m_height = static_cast<int>(h);

    // Resolve output (decode) resolution from the video-quality setting. A
    // smaller decode size shrinks the ring buffer, GL textures, PBOs and the
    // resolve FBO — the dominant video RAM consumers. Integer factor keeps the
    // aspect ratio and avoids chroma-alignment artifacts; dims stay even for
    // 4:2:0. High quality (cap 0) keeps native resolution and the zero-overhead
    // direct-copy path.
    m_outWidth  = m_width;
    m_outHeight = m_height;
    m_downscaleFactor = 1;
    int cap = paimon::settings::video::videoMaxDecodeDimension();
    if (cap > 0) {
        int maxDim = std::max(m_width, m_height);
        if (maxDim > cap) {
            int f = std::min((maxDim + cap - 1) / cap, 4);
            if (f >= 2) {
                m_downscaleFactor = f;
                m_outWidth  = std::max(2, (m_width  / f) & ~1);
                m_outHeight = std::max(2, (m_height / f) & ~1);
                geode::log::info("DecoderMF: downscaling {}x{} -> {}x{} (factor {}, quality cap {})",
                    m_width, m_height, m_outWidth, m_outHeight, f, cap);
            }
        }
    }

    PROPVARIANT var;
    hr = m_reader->GetPresentationAttribute(
        static_cast<UINT32>(MF_SOURCE_READER_MEDIASOURCE),
        MF_PD_DURATION, &var);
    if (SUCCEEDED(hr)) {
        if (var.vt == VT_UI8) {
            m_duration = static_cast<double>(var.uhVal.QuadPart) / 10000000.0;
        }
        PropVariantClear(&var);
    }

    return true;
}

bool DecoderMF::setOutputFormat() {
    const GUID formatsToTry[] = {
        MFVideoFormat_NV12,  // native MF format — no conversion, unambiguous CbCr order
        MFVideoFormat_I420,  // Y→Cb→Cr
        MFVideoFormat_YV12,  // common on Windows — Y→Cr→Cb, needs swap
    };

    for (const auto& fmt : formatsToTry) {
        IMFMediaType* type = nullptr;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) continue;

        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, fmt);

        hr = m_reader->SetCurrentMediaType(
            static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr, type);
        type->Release();

        if (SUCCEEDED(hr)) {
            m_subType = fmt;
            m_pixelFormat = fmt;
            const char* name =
                fmt == MFVideoFormat_NV12 ? "NV12" :
                fmt == MFVideoFormat_I420 ? "I420" : "YV12";
            geode::log::info("DecoderMF: output format set to {}", name);
            return true;
        }
    }
    return false;
}

// Derive the real Y-plane height (in rows) from the total size of the contiguous
// 4:2:0 buffer (Y + chroma occupy stride * yRows * 3/2 bytes).
//
// Avoids guessing the decoder's alignment with (height + 15) & ~15: codecs pad the
// Y plane to 16/32/no boundary, and a wrong guess lands the chroma offset on zeros,
// so the YUV->RGB shader reads Cb=Cr=0 and the video looks GREEN. Falls back to the
// 16-px heuristic if the buffer size isn't a coherent 4:2:0 frame.
static int deriveYPlaneRows(size_t bufferSize, int stride, int visibleHeight) {
    int heuristic = (visibleHeight + 15) & ~15;
    if (bufferSize == 0 || stride <= 0 || visibleHeight <= 0) return heuristic;

    // total = stride * yRows * 3/2  →  yRows = bufferSize * 2 / (stride * 3)
    long long derived = static_cast<long long>(bufferSize) * 2 /
                        (static_cast<long long>(stride) * 3);

    // Must cover at least the visible height and not exceed a plausible padding
    // (up to 256 rows) to reject Y-only or over-allocated buffers.
    if (derived >= visibleHeight && derived <= static_cast<long long>(visibleHeight) + 256) {
        return static_cast<int>(derived);
    }
    return heuristic;
}

// Plane copying — 2D buffer path (stride-aware)
void DecoderMF::copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot, size_t bufferSize) {
    // Media Foundation can return a negative stride for bottom-up frames.
    // In that case scanline0 points to the LAST visible row and lStride is
    // the negative byte-step between rows. The arithmetic below assumes a
    // positive top-down layout, so normalise here.
    if (lStride < 0) {
        scanline0 = scanline0 + static_cast<ptrdiff_t>(lStride) * (m_height - 1);
        lStride = -lStride;
    }

    int uvW = (m_width + 1) / 2;
    int uvH = (m_height + 1) / 2;
    int uvSrcStride = (lStride + 1) / 2;  // chroma stride for planar formats

    // UV plane starts after the FULL (padded) Y plane. The decoder may pad the
    // Y plane to a macroblock boundary (16/32 px) or not at all. Instead of
    // guessing the alignment we derive the real Y-plane height from the actual
    // buffer size — guessing wrong makes the chroma offset land on zeros and
    // the video renders GREEN. Falls back to the 16-px heuristic if the buffer
    // size is unknown/incoherent.
    int alignedH = deriveYPlaneRows(bufferSize, static_cast<int>(lStride), m_height);
    int alignedUvH = (alignedH + 1) / 2;

    // Y plane — bulk copy when strides match, row-by-row otherwise
    int yCopyBytes = std::min(slot.strideY, static_cast<int>(lStride));
    if (slot.strideY == lStride && slot.strideY >= m_width) {
        // Strides match — single bulk memcpy for all visible Y rows
        std::memcpy(slot.planeY, scanline0, static_cast<size_t>(yCopyBytes) * m_height);
    } else {
        for (int r = 0; r < m_height; ++r) {
            std::memcpy(slot.planeY + r * slot.strideY,
                        scanline0 + r * lStride, yCopyBytes);
        }
    }

    if (m_pixelFormat == MFVideoFormat_I420) {
        // I420: Y → Cb → Cr — direct copy, correct order
        BYTE* cbStart = scanline0 + lStride * alignedH;
        BYTE* crStart = cbStart + uvSrcStride * alignedUvH;
        if (slot.strideCb == uvSrcStride && slot.strideCr == uvSrcStride) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvSrcStride) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvSrcStride) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb,
                            cbStart + r * uvSrcStride,
                            std::min(slot.strideCb, uvW));
                std::memcpy(slot.planeCr + r * slot.strideCr,
                            crStart + r * uvSrcStride,
                            std::min(slot.strideCr, uvW));
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_YV12) {
        // YV12: Y → Cr → Cb — swap Cb/Cr so shader gets correct order
        BYTE* crStart = scanline0 + lStride * alignedH;
        BYTE* cbStart = crStart + uvSrcStride * alignedUvH;
        if (slot.strideCb == uvSrcStride && slot.strideCr == uvSrcStride) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvSrcStride) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvSrcStride) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb,
                            cbStart + r * uvSrcStride,
                            std::min(slot.strideCb, uvW));
                std::memcpy(slot.planeCr + r * slot.strideCr,
                            crStart + r * uvSrcStride,
                            std::min(slot.strideCr, uvW));
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_NV12) {
        // NV12: Y + interleaved [Cb,Cr] pairs.
        // SSE2-vectorised deinterleave (16 pairs/iter); ~6-8x faster than scalar.
        BYTE* uvStart = scanline0 + lStride * alignedH;
        int uvStride = lStride;
        for (int r = 0; r < uvH; ++r) {
            deinterleaveNV12Row(uvStart + r * uvStride,
                                slot.planeCb + r * slot.strideCb,
                                slot.planeCr + r * slot.strideCr,
                                uvW);
        }
    } else {
        // Unsupported chroma format (e.g. YUY2 4:2:2, P010 10-bit). Cb/Cr planes are
        // left unwritten -> the shader reads them as 0 and the video looks green. Warn once.
        static bool s_warnedFmt = false;
        if (!s_warnedFmt) {
            s_warnedFmt = true;
            geode::log::warn("DecoderMF: unhandled pixel format (not NV12/I420/YV12) — "
                             "chroma not extracted, video may render green");
        }
    }
}

// Plane copying — linear buffer fallback (no stride)
void DecoderMF::copyPlanesToSlotLinear(BYTE* data, DWORD bufLen, Frame& slot) {
    int uvW    = (m_width + 1) / 2;
    int uvH    = (m_height + 1) / 2;

    // Derive the real Y-plane height from the buffer size instead of guessing
    // the decoder's macroblock alignment (16/32/none). A wrong guess makes the
    // chroma offset land on zeros → the video renders green. Stride == width
    // for a contiguous linear buffer.
    int alignedH = deriveYPlaneRows(static_cast<size_t>(bufLen), m_width, m_height);
    int alignedUvH = (alignedH + 1) / 2;
    int ySize  = m_width * alignedH;
    int uvSize = uvW * alignedUvH;

    // Y plane — bulk copy when stride matches width
    if (slot.strideY == m_width) {
        std::memcpy(slot.planeY, data, static_cast<size_t>(m_width) * m_height);
    } else {
        for (int r = 0; r < m_height; ++r) {
            std::memcpy(slot.planeY + r * slot.strideY,
                        data + r * m_width, m_width);
        }
    }

    if (m_pixelFormat == MFVideoFormat_I420) {
        // I420: Y → Cb → Cr
        BYTE* cbStart = data + ySize;
        BYTE* crStart = cbStart + uvSize;
        if (slot.strideCb == uvW && slot.strideCr == uvW) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvW) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvW) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb, cbStart + r * uvW, uvW);
                std::memcpy(slot.planeCr + r * slot.strideCr, crStart + r * uvW, uvW);
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_YV12) {
        // YV12: Y → Cr → Cb — swap
        BYTE* crStart = data + ySize;
        BYTE* cbStart = crStart + uvSize;
        if (slot.strideCb == uvW && slot.strideCr == uvW) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvW) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvW) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb, cbStart + r * uvW, uvW);
                std::memcpy(slot.planeCr + r * slot.strideCr, crStart + r * uvW, uvW);
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_NV12) {
        // NV12: Y + interleaved [Cb,Cr] pairs (SSE2-vectorised deinterleave)
        BYTE* uvStart = data + ySize;
        for (int r = 0; r < uvH; ++r) {
            deinterleaveNV12Row(uvStart + r * m_width,
                                slot.planeCb + r * slot.strideCb,
                                slot.planeCr + r * slot.strideCr,
                                uvW);
        }
    }
}

// D3D11 staging texture for GPU→CPU readback (DXVA path)
bool DecoderMF::createStagingTexture() {
    if (!m_d3dDevice || m_width <= 0 || m_height <= 0) return false;

    // Release previous staging texture if dimensions changed
    if (m_stagingTex) {
        m_stagingTex->Release();
        m_stagingTex = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(m_width);
    desc.Height = static_cast<UINT>(m_height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    // NV12 is the most common DXVA output format — 2 planes in one texture
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTex);
    if (FAILED(hr) || !m_stagingTex) {
        geode::log::warn("DecoderMF: failed to create staging texture ({}x{})", m_width, m_height);
        return false;
    }
    geode::log::info("DecoderMF: staging texture created ({}x{}, NV12)", m_width, m_height);
    return true;
}

bool DecoderMF::copyPlanesFromD3D11(ID3D11Texture2D* srcTexture, UINT subresource, Frame& slot) {
    if (!m_d3dCtx || !srcTexture) return false;

    // Query source texture format — must match staging texture for CopySubresourceRegion
    D3D11_TEXTURE2D_DESC srcDesc = {};
    srcTexture->GetDesc(&srcDesc);

    // 420_OPAQUE and other opaque formats cannot be read by the CPU
    if (srcDesc.Format == DXGI_FORMAT_420_OPAQUE ||
        srcDesc.Format == DXGI_FORMAT_AI44 ||
        srcDesc.Format == DXGI_FORMAT_IA44 ||
        srcDesc.Format == DXGI_FORMAT_P8 ||
        srcDesc.Format == DXGI_FORMAT_A8P8 ||
        srcDesc.Format == DXGI_FORMAT_UNKNOWN) {
        geode::log::warn("DecoderMF: DXVA output format {} is not CPU-readable, falling back",
            static_cast<int>(srcDesc.Format));
        return false;
    }

    // (Re)create staging texture if format or dimensions changed
    if (!m_stagingTex || m_stagingFormat != srcDesc.Format ||
        m_stagingWidth != srcDesc.Width || m_stagingHeight != srcDesc.Height) {
        if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = srcDesc.Width;
        desc.Height = srcDesc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = srcDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTex);
        if (FAILED(hr) || !m_stagingTex) {
            geode::log::warn("DecoderMF: failed to create staging texture ({}x{}, format={})",
                srcDesc.Width, srcDesc.Height, static_cast<int>(srcDesc.Format));
            return false;
        }
        m_stagingFormat = srcDesc.Format;
        m_stagingWidth = srcDesc.Width;
        m_stagingHeight = srcDesc.Height;
        geode::log::info("DecoderMF: staging texture created ({}x{}, format={})",
            srcDesc.Width, srcDesc.Height, static_cast<int>(srcDesc.Format));
    }

    // All D3D11 context calls must be serialised — the DXVA decode engine
    // inside msmpeg2vdec.dll submits GPU work on this same context concurrently.
    // Without the lock AMD drivers (atidxx64.dll) can null-deref internally.
    // NOTE: ID3D11Multithread provides driver-level protection, but we add an
    // application-level lock as belt-and-suspenders for drivers that ignore the flag.
    {
        std::lock_guard<std::mutex> ctxLk(m_d3dCtxMutex);

        // Copy from GPU decode texture to CPU-accessible staging texture
        m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0, srcTexture, subresource, nullptr);

        // Map staging texture for CPU read
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            geode::log::warn("DecoderMF: failed to map staging texture (hr={:08X})", static_cast<unsigned>(hr));
            return false;
        }

        // NV12 layout: Y plane first, then interleaved CbCr plane
        BYTE* scanline0 = static_cast<BYTE*>(mapped.pData);
        LONG lStride = static_cast<LONG>(mapped.RowPitch);

        // The UV plane of an NV12 staging texture starts exactly at RowPitch *
        // texture-height. Pass the real mapped buffer size so deriveYPlaneRows recovers
        // the exact Y-plane height (m_stagingHeight) instead of the 16px heuristic, which
        // fails on drivers that align to 32/64 px and tints the chroma green.
        size_t mappedSize = static_cast<size_t>(mapped.RowPitch) * m_stagingHeight * 3 / 2;
        copyPlanesToSlot2D(scanline0, lStride, slot, mappedSize);

        m_d3dCtx->Unmap(m_stagingTex, 0);
    }
    return true;
}

void DecoderMF::startDecoding() {
    // A detached worker (its join timed out) may still be running decodeLoop;
    // spawning a second thread would put two producers on the SPSC ring and call
    // the non-thread-safe IMFSourceReader concurrently. Treat detached as
    // terminal, matching DecoderPLM/DecoderAVF and the isTerminal() contract that
    // VideoPlayer relies on to drop and recreate the decoder.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    if (m_decoding.load(std::memory_order_relaxed)) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    m_thread = std::thread(&DecoderMF::decodeLoop, this);
}

void DecoderMF::stopDecoding() {
    // Cap join time so a stuck Media Foundation ReadSample() doesn't freeze
    // the main thread. Detach on timeout (leak-on-terminal keeps it safe).
    constexpr auto kJoinTimeout = std::chrono::milliseconds(1000);

    bool wasDecoding = m_decoding.exchange(false, std::memory_order_acq_rel);
    m_ring.wakeAll();  // unblock any cv waits ASAP
    if (!wasDecoding) {
        // Was not decoding — but thread might still be joinable from a previous run
        if (m_thread.joinable()) {
            if (!paimon::timedJoin(m_thread, kJoinTimeout)) {
                m_decodeThreadDetached.store(true, std::memory_order_release);
            }
        }
        return;
    }

    // Flush cancels any pending synchronous ReadSample() call on the decode thread,
    // allowing it to observe m_decoding == false and exit the loop.
    // During force close, MF DLLs may already be unloaded — use SEH to avoid
    // crashing when the COM vtable points into unloaded code.
    if (m_reader) {
        __try {
            m_reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM));
        } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
                   ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            // MF DLLs already unloaded during force close — Flush not needed
        }
    }

    if (m_thread.joinable()) {
        if (!paimon::timedJoin(m_thread, kJoinTimeout)) {
            m_decodeThreadDetached.store(true, std::memory_order_release);
        }
    }
}

void DecoderMF::decodeLoop() {
    // Initialize COM for this thread — required for Media Foundation
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Raise decode thread priority to reduce ReadSample latency
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    int frameCount = 0;
    while (m_decoding.load(std::memory_order_relaxed)) {
        if (m_ring.isFull()) {
            // Wait up to 50 ms for a slot to free; cv is poked on commitRead/skipRead.
            // Falling through on timeout is fine — m_decoding is re-checked at loop top.
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        DWORD streamIdx = 0, flags = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = m_reader->ReadSample(
            static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIdx, &flags, nullptr, &sample);

        if (FAILED(hr) || !m_decoding.load(std::memory_order_relaxed)) {
            // ReadSample failed or was cancelled by Flush() during shutdown
            if (sample) sample->Release();
            if (FAILED(hr) && m_decoding.load(std::memory_order_relaxed)) {
                geode::log::warn("DecoderMF: ReadSample failed (hr={})", hr);
            }
            m_finished.store(true, std::memory_order_release);
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            geode::log::info("DecoderMF: end of stream after {} frames", frameCount);
            m_finished.store(true, std::memory_order_release);
            break;
        }

        // After Flush(), ReadSample returns S_OK with null sample and FLUSHED flag
        // MF_SOURCE_READERF_FLUSHED = 0x200 (not always in older SDK headers)
        constexpr DWORD kFlushFlag = 0x200;
        if (flags & kFlushFlag) {
            if (sample) sample->Release();
            continue; // loop will re-check m_decoding at the top
        }

        if (!sample) continue;

        auto* slot = m_ring.nextWrite();
        if (!slot) {
            sample->Release();
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        LONGLONG pts100ns = 0;
        sample->GetSampleTime(&pts100ns);
        slot->pts = static_cast<double>(pts100ns) / 10000000.0;

        IMFMediaBuffer* buf = nullptr;
        hr = sample->GetBufferByIndex(0, &buf);
        if (FAILED(hr) || !buf) {
            sample->Release();
            continue;
        }

        DWORD bufLen = 0;
        buf->GetCurrentLength(&bufLen);

        bool copied = false;

        // When downscaling, decode into the native-sized scratch then box-average
        // into the (smaller) ring slot. Otherwise write straight into the slot.
        Frame* dst = (m_downscaleFactor > 1) ? &m_scratch : slot;

        // Path 1: D3D11 surface (DXVA hardware decode)
        // When DXVA is active, the buffer may contain a D3D11 texture surface
        // that cannot be locked via IMF2DBuffer2. Use staging texture readback.
        if (m_dxvaEnabled && m_d3dCtx) {
            IMFDXGIBuffer* dxgiBuf = nullptr;
            hr = buf->QueryInterface(IID_PPV_ARGS(&dxgiBuf));
            if (SUCCEEDED(hr) && dxgiBuf) {
                ID3D11Texture2D* tex = nullptr;
                UINT subresource = 0;
                hr = dxgiBuf->GetResource(IID_PPV_ARGS(&tex));
                if (SUCCEEDED(hr) && tex) {
                    dxgiBuf->GetSubresourceIndex(&subresource);

                    copied = copyPlanesFromD3D11(tex, subresource, *dst);

                    tex->Release();
                }
                dxgiBuf->Release();
            }
        }

        // Path 2: IMF2DBuffer2 for direct CPU plane access (software decode)
        if (!copied) {
            IMF2DBuffer2* buf2d = nullptr;
            hr = buf->QueryInterface(IID_PPV_ARGS(&buf2d));
            if (SUCCEEDED(hr) && buf2d) {
                BYTE* scanline0 = nullptr;
                LONG lStride = 0;
                DWORD cbBuffer = 0;
                hr = buf2d->Lock2DSize(MF2DBuffer_LockFlags_Read,
                                       &scanline0, &lStride,
                                       nullptr, &cbBuffer);
                if (SUCCEEDED(hr)) {
                    copyPlanesToSlot2D(scanline0, lStride, *dst, static_cast<size_t>(cbBuffer));
                    copied = true;
                    buf2d->Unlock2D();
                }
                buf2d->Release();
            }
        }

        // Path 3: Fallback linear buffer lock
        if (!copied) {
            BYTE* data = nullptr;
            hr = buf->Lock(&data, nullptr, &bufLen);
            if (SUCCEEDED(hr) && data) {
                copyPlanesToSlotLinear(data, bufLen, *dst);
                copied = true;
                buf->Unlock();
            }
        }

        buf->Release();
        sample->Release();

        if (copied) {
            if (m_downscaleFactor > 1) {
                downscalePlanes(m_scratch, *slot, m_downscaleFactor);
            }
            m_dxvaReadbackFailures = 0;
            m_ring.commitWrite();
            ++frameCount;
            if (frameCount == 1) {
                geode::log::info("DecoderMF: first frame decoded ({}x{}, format={}, dxva={})", m_width, m_height,
                    m_pixelFormat == MFVideoFormat_I420 ? "I420" :
                    m_pixelFormat == MFVideoFormat_YV12 ? "YV12" : "NV12",
                    m_dxvaEnabled ? "yes" : "no");
            }
        } else if (m_dxvaEnabled) {
            ++m_dxvaReadbackFailures;
            if (m_dxvaReadbackFailures >= 3) {
                geode::log::warn("DecoderMF: DXVA readback failed {} times, falling back to software decode",
                    m_dxvaReadbackFailures);
                if (fallbackToSoftwareDecode(m_videoPath)) {
                    geode::log::info("DecoderMF: switched to software decode, continuing");
                } else {
                    geode::log::warn("DecoderMF: software decode fallback failed, stopping");
                    m_finished.store(true, std::memory_order_release);
                    break;
                }
            }
        }
    }

    CoUninitialize();
}


// DXVA fallback — recreate reader for software decode
bool DecoderMF::fallbackToSoftwareDecode(const std::string& path) {
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;

    // Release staging texture (no longer needed) — lock to prevent racing with
    // concurrent copyPlanesFromD3D11 calls still running on the decode thread.
    {
        std::lock_guard<std::mutex> ctxLk(m_d3dCtxMutex);
        if (m_stagingTex) {
            m_stagingTex->Release();
            m_stagingTex = nullptr;
        }
    }
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;

    // Release old reader
    if (m_reader) {
        m_reader->Release();
        m_reader = nullptr;
    }

    // Create a new reader WITHOUT the D3D manager to force software decode.
    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) return false;

    hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to set MF_LOW_LATENCY");
    }

    hr = attrs->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to disable DXVA");
    }

    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wLen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, nullptr, 0);
    if (wLen <= 0) { attrs->Release(); return false; }
    auto* wPath = new (std::nothrow) wchar_t[wLen];
    if (!wPath) { attrs->Release(); return false; }
    MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wPath, wLen);

    hr = MFCreateSourceReaderFromURL(wPath, attrs, &m_reader);
    delete[] wPath;
    attrs->Release();

    if (FAILED(hr) || !m_reader) {
        geode::log::warn("DecoderMF: fallback - MFCreateSourceReaderFromURL failed (hr={})", hr);
        return false;
    }

    if (!setOutputFormat()) {
        geode::log::warn("DecoderMF: fallback - failed to set output format");
        return false;
    }

    geode::log::info("DecoderMF: successfully switched to software decode");
    return true;
}

void DecoderMF::seekTo(double seconds) {
    if (!m_reader) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;

    while (m_ring.nextRead()) m_ring.commitRead();

    PROPVARIANT var;
    var.vt = VT_I8;
    var.hVal.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);
    m_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    m_finished.store(false, std::memory_order_relaxed);
    if (wasDecoding) startDecoding();
}

// Box-average a single 8-bit plane by an integer factor. Averages each f×f
// source block into one destination pixel; edge blocks clamp to plane bounds.
static void boxDownscalePlane(const uint8_t* src, int srcStride, int srcW, int srcH,
                              uint8_t* dst, int dstStride, int dstW, int dstH, int f) {
    for (int dy = 0; dy < dstH; ++dy) {
        int sy0 = dy * f;
        int sy1 = std::min(sy0 + f, srcH);
        uint8_t* dstRow = dst + dy * dstStride;
        for (int dx = 0; dx < dstW; ++dx) {
            int sx0 = dx * f;
            int sx1 = std::min(sx0 + f, srcW);
            unsigned sum = 0, count = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* srcRow = src + sy * srcStride;
                for (int sx = sx0; sx < sx1; ++sx) {
                    sum += srcRow[sx];
                    ++count;
                }
            }
            dstRow[dx] = count ? static_cast<uint8_t>(sum / count) : 0;
        }
    }
}

void DecoderMF::downscalePlanes(const Frame& src, Frame& dst, int factor) {
    boxDownscalePlane(src.planeY, src.strideY, m_width, m_height,
                      dst.planeY, dst.strideY, m_outWidth, m_outHeight, factor);
    int srcUvW = (m_width + 1) / 2,  srcUvH = (m_height + 1) / 2;
    int dstUvW = (m_outWidth + 1) / 2, dstUvH = (m_outHeight + 1) / 2;
    boxDownscalePlane(src.planeCb, src.strideCb, srcUvW, srcUvH,
                      dst.planeCb, dst.strideCb, dstUvW, dstUvH, factor);
    boxDownscalePlane(src.planeCr, src.strideCr, srcUvW, srcUvH,
                      dst.planeCr, dst.strideCr, dstUvW, dstUvH, factor);
}

bool DecoderMF::consumeFrame(Frame& outFrame) {
    auto* slot = m_ring.nextRead();
    if (!slot) return false;

    if (outFrame.strideY == slot->strideY) {
        std::memcpy(outFrame.planeY, slot->planeY,
                    static_cast<size_t>(outFrame.strideY) * slot->height);
    } else {
        int copyBytes = std::min(outFrame.strideY, slot->strideY);
        for (int r = 0; r < slot->height; ++r)
            std::memcpy(outFrame.planeY + r * outFrame.strideY,
                        slot->planeY + r * slot->strideY, copyBytes);
    }

    int uvH = (slot->height + 1) / 2;
    if (outFrame.strideCb == slot->strideCb && outFrame.strideCr == slot->strideCr) {
        std::memcpy(outFrame.planeCb, slot->planeCb,
                    static_cast<size_t>(outFrame.strideCb) * uvH);
        std::memcpy(outFrame.planeCr, slot->planeCr,
                    static_cast<size_t>(outFrame.strideCr) * uvH);
    } else {
        int cbBytes = std::min(outFrame.strideCb, slot->strideCb);
        int crBytes = std::min(outFrame.strideCr, slot->strideCr);
        for (int r = 0; r < uvH; ++r) {
            std::memcpy(outFrame.planeCb + r * outFrame.strideCb,
                        slot->planeCb + r * slot->strideCb, cbBytes);
            std::memcpy(outFrame.planeCr + r * outFrame.strideCr,
                        slot->planeCr + r * slot->strideCr, crBytes);
        }
    }

    outFrame.pts = slot->pts;
    m_ring.commitRead();
    return true;
}

bool DecoderMF::skipFrame() {
    return m_ring.skipRead();
}

double DecoderMF::getDuration() const { return m_duration; }
int DecoderMF::getWidth()  const { return m_outWidth  > 0 ? m_outWidth  : m_width; }
int DecoderMF::getHeight() const { return m_outHeight > 0 ? m_outHeight : m_height; }
bool DecoderMF::isFinished() const {
    return m_finished.load(std::memory_order_acquire);
}

double DecoderMF::peekNextPTS() const {
    return m_ring.peekNextPTS();
}

double DecoderMF::peekSecondPTS() const {
    return m_ring.peekSecondPTS();
}

const VideoFrame* DecoderMF::peekFrame() {
    return m_ring.peekRead();
}

void DecoderMF::releaseFrame() {
    if (m_ring.peekRead()) m_ring.commitRead();
}

void DecoderMF::closeInternal() {
    stopDecoding();

    // If the decode thread was detached due to a timedJoin timeout, it may
    // still be executing ReadSample() or DXVA readback and holding references
    // to m_reader and the D3D objects.  We still attempt Release() under SEH
    // so that resources are freed when possible; if the DLLs are already
    // unloaded the exception handler silently nulls the pointers.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
        geode::log::warn("[DecoderMF] closeInternal: decode thread was detached; "
                         "forcing COM/D3D release under SEH.");
        {
            std::lock_guard lk(g_d3d11Mutex);
            if (m_sharedD3D) {
                m_d3dDevice = nullptr;
                m_d3dCtx    = nullptr;
                if (m_dxgiMgr) { m_dxgiMgr->Release(); m_dxgiMgr = nullptr; }
                releaseSharedD3D11();
                m_sharedD3D = false;
            } else {
                releaseD3D11Safely(m_d3dDevice, m_d3dCtx, m_dxgiMgr);
            }
        }
        releaseMfObjectsSafely(m_stagingTex, m_reader);
        m_dxvaEnabled = false;
        m_dxvaReadbackFailures = 0;
        m_stagingFormat = DXGI_FORMAT_UNKNOWN;
        m_stagingWidth  = 0;
        m_stagingHeight = 0;
        m_videoPath.clear();
        return;
    }

    // Release D3D11 resources.  When m_sharedD3D is set the device + context
    // are owned by acquireSharedD3D11()'s static cache and MUST NOT be
    // Release()d here — other decoders are still using them.  Only release
    // the per-decoder dxgiMgr.
    {
        std::lock_guard lk(g_d3d11Mutex);
        if (m_dxgiMgr) {
            m_dxgiMgr->Release();
            m_dxgiMgr = nullptr;
        }
        if (m_sharedD3D) {
            // Just drop pointers — caller no longer owns the device.
            m_d3dCtx    = nullptr;
            m_d3dDevice = nullptr;
        } else {
            if (m_d3dCtx) {
                m_d3dCtx->Release();
                m_d3dCtx = nullptr;
            }
            if (m_d3dDevice) {
                m_d3dDevice->Release();
                m_d3dDevice = nullptr;
            }
        }
    }
    if (m_sharedD3D) {
        releaseSharedD3D11();
        m_sharedD3D = false;
    }

    // During force close, MF DLLs (msmpeg2vdec.dll etc.) may already be unloaded
    // before the static singleton destructor runs. COM Release calls would crash
    // with ACCESS_VIOLATION because the vtable points into unloaded code.
    // Use SEH to handle this gracefully — just null out the pointers.
    releaseMfObjectsSafely(m_stagingTex, m_reader);
    // dxgiMgr / d3dCtx / d3dDevice already released above
    // NOTE: Do NOT call MFShutdown() here - it closes the platform for ALL
    // decoders and causes MF_E_PLATFORM_NOT_INITIALIZED when creating a new
    // decoder after the old one is destroyed. MF will be cleaned up automatically
    // when the process exits.
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;
    m_videoPath.clear();

    // Decode thread is joined here, so the scratch is no longer in use.
    Frame::freeAligned(m_scratch.planeY);
    Frame::freeAligned(m_scratch.planeCb);
    Frame::freeAligned(m_scratch.planeCr);
    m_scratch.planeY = m_scratch.planeCb = m_scratch.planeCr = nullptr;
    m_downscaleFactor = 1;
}

} // namespace paimon

#endif // USE_MEDIA_FOUNDATION
