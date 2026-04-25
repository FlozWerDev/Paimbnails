#include "DecoderMF.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <objbase.h>   // CoInitializeEx / CoUninitialize
#include "../../utils/TimedJoin.hpp"

namespace paimon {

namespace {
// Global mutex that serialises D3D11 device creation / destruction.
// Creating or destroying multiple D3D11 hardware devices concurrently
// deadlocks some GPU drivers. Each decoder gets its OWN device/context/
// dxgiMgr; we just ensure no two threads touch the driver simultaneously
// during init/shutdown.
std::mutex g_d3d11Mutex;
} // namespace

// ─────────────────────────────────────────────────────────────
// Open
// ─────────────────────────────────────────────────────────────
bool DecoderMF::open(const std::string& path) {
    closeInternal();
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

    if (!m_ring.init(m_width, m_height)) {
        closeInternal();
        return false;
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

bool DecoderMF::setupD3D11() {
    std::lock_guard lk(g_d3d11Mutex);

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL outLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0x4,  // D3D11_CREATE_DEVICE_MULTITHREADED
        levels, 2, D3D11_SDK_VERSION,
        &m_d3dDevice, &outLevel, &m_d3dCtx);

    if (FAILED(hr) || !m_d3dDevice) {
        geode::log::warn("DecoderMF: D3D11CreateDevice failed (hr={:08X})", static_cast<unsigned>(hr));
        return false;
    }

    hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_dxgiMgr);
    if (FAILED(hr) || !m_dxgiMgr) {
        geode::log::warn("DecoderMF: MFCreateDXGIDeviceManager failed (hr={:08X})", static_cast<unsigned>(hr));
        m_d3dDevice->Release(); m_d3dDevice = nullptr;
        m_d3dCtx->Release();    m_d3dCtx    = nullptr;
        return false;
    }

    hr = m_dxgiMgr->ResetDevice(m_d3dDevice, m_resetToken);
    m_dxvaEnabled = SUCCEEDED(hr);
    if (!m_dxvaEnabled) {
        geode::log::warn("DecoderMF: DXGI manager ResetDevice failed, DXVA unavailable");
    }

    geode::log::info("DecoderMF: created private D3D11 device (DXVA={})", m_dxvaEnabled ? "yes" : "no");
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

    // Wide string conversion for MFCreateSourceReaderFromURL
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

    // Retrieve actual dimensions from the reader
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

    // Duration
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

// ─────────────────────────────────────────────────────────────
// Output format selection
// ─────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// Plane copying — 2D buffer path (stride-aware)
// ─────────────────────────────────────────────────────────────
void DecoderMF::copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot) {
    int uvW = (m_width + 1) / 2;
    int uvH = (m_height + 1) / 2;
    int uvSrcStride = (lStride + 1) / 2;  // chroma stride for planar formats

    // H.264/HEVC decoders pad height to macroblock boundary (multiple of 16).
    // UV plane starts after the padded Y plane, not the visible height.
    // e.g. 1080 → 1088, but 720 and 1440 are already multiples of 16.
    int alignedH = (m_height + 15) & ~15;
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
        // NV12: Y + interleaved [Cb,Cr] pairs
        // Optimized: deinterleave Cb/Cr per row using two memcpy passes
        // instead of per-pixel byte access. This is ~3-5x faster for 1080p+.
        BYTE* uvStart = scanline0 + lStride * alignedH;
        int uvStride = lStride;
        for (int r = 0; r < uvH; ++r) {
            const BYTE* uvRow = uvStart + r * uvStride;
            uint8_t* cbRow = slot.planeCb + r * slot.strideCb;
            uint8_t* crRow = slot.planeCr + r * slot.strideCr;
            // Deinterleave: extract even bytes (Cb) and odd bytes (Cr)
            for (int c = 0; c < uvW; ++c) {
                cbRow[c] = uvRow[c * 2];
                crRow[c] = uvRow[c * 2 + 1];
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Plane copying — linear buffer fallback (no stride)
// ─────────────────────────────────────────────────────────────
void DecoderMF::copyPlanesToSlotLinear(BYTE* data, DWORD /*bufLen*/, Frame& slot) {
    int uvW    = (m_width + 1) / 2;
    int uvH    = (m_height + 1) / 2;

    // H.264/HEVC pad height to macroblock boundary (multiple of 16)
    int alignedH = (m_height + 15) & ~15;
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
        // NV12: Y + interleaved [Cb,Cr] pairs
        BYTE* uvStart = data + ySize;
        for (int r = 0; r < uvH; ++r) {
            const BYTE* uvRow = uvStart + r * m_width;
            uint8_t* cbRow = slot.planeCb + r * slot.strideCb;
            uint8_t* crRow = slot.planeCr + r * slot.strideCr;
            for (int c = 0; c < uvW; ++c) {
                cbRow[c] = uvRow[c * 2];
                crRow[c] = uvRow[c * 2 + 1];
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// D3D11 staging texture for GPU→CPU readback (DXVA path)
// ─────────────────────────────────────────────────────────────
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

    // Copy from GPU decode texture to CPU-accessible staging texture
    m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0, srcTexture, subresource, nullptr);

    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: failed to map staging texture");
        return false;
    }

    // NV12 layout: Y plane first, then interleaved CbCr plane
    BYTE* scanline0 = static_cast<BYTE*>(mapped.pData);
    LONG lStride = static_cast<LONG>(mapped.RowPitch);

    // Use the existing 2D copy function (handles NV12 deinterleaving)
    copyPlanesToSlot2D(scanline0, lStride, slot);

    m_d3dCtx->Unmap(m_stagingTex, 0);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Decode loop
// ─────────────────────────────────────────────────────────────
void DecoderMF::startDecoding() {
    if (m_decoding.load(std::memory_order_relaxed)) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    uint64_t gen = ++m_generation;
    m_thread = std::thread([this, gen]() { decodeLoop(gen); });
}

bool DecoderMF::stopDecoding() {
    bool wasDecoding = m_decoding.exchange(false, std::memory_order_acq_rel);
    if (!wasDecoding) {
        // Was not decoding — but thread might still be joinable from a previous run
        if (m_thread.joinable()) paimon::timedJoin(m_thread, std::chrono::seconds(3));
        return true;  // was already stopped; no active decode race to worry about
    }

    ++m_generation;  // invalidate current decode thread's generation

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

    bool joined = true;
    if (m_thread.joinable()) joined = paimon::timedJoin(m_thread, std::chrono::seconds(3));
    return joined;
}

void DecoderMF::decodeLoop(uint64_t gen) {
    // Initialize COM for this thread — required for Media Foundation
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Raise decode thread priority to reduce ReadSample latency
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    m_threadRunning.store(true, std::memory_order_release);

    int frameCount = 0;
    while (m_decoding.load(std::memory_order_relaxed) &&
           gen == m_generation.load(std::memory_order_relaxed)) {
        if (m_ring.isFull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Get PTS
        LONGLONG pts100ns = 0;
        sample->GetSampleTime(&pts100ns);
        slot->pts = static_cast<double>(pts100ns) / 10000000.0;

        // Extract buffer
        IMFMediaBuffer* buf = nullptr;
        hr = sample->GetBufferByIndex(0, &buf);
        if (FAILED(hr) || !buf) {
            sample->Release();
            continue;
        }

        DWORD bufLen = 0;
        buf->GetCurrentLength(&bufLen);

        bool copied = false;

        // ── Path 1: D3D11 surface (DXVA hardware decode) ──
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

                    copied = copyPlanesFromD3D11(tex, subresource, *slot);

                    tex->Release();
                }
                dxgiBuf->Release();
            }
        }

        // ── Path 2: IMF2DBuffer2 for direct CPU plane access (software decode) ──
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
                    copyPlanesToSlot2D(scanline0, lStride, *slot);
                    copied = true;
                    buf2d->Unlock2D();
                }
                buf2d->Release();
            }
        }

        // ── Path 3: Fallback linear buffer lock ──
        if (!copied) {
            BYTE* data = nullptr;
            hr = buf->Lock(&data, nullptr, &bufLen);
            if (SUCCEEDED(hr) && data) {
                copyPlanesToSlotLinear(data, bufLen, *slot);
                copied = true;
                buf->Unlock();
            }
        }

        buf->Release();
        sample->Release();

        if (copied) {
            m_dxvaReadbackFailures = 0;  // reset on success
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

    m_threadRunning.store(false, std::memory_order_release);
    CoUninitialize();
}


// ─────────────────────────────────────────────────────────────
// DXVA fallback — recreate reader for software decode
// ─────────────────────────────────────────────────────────────
bool DecoderMF::fallbackToSoftwareDecode(const std::string& path) {
    // Disable DXVA for this decoder instance
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;

    // Release staging texture (no longer needed)
    if (m_stagingTex) {
        m_stagingTex->Release();
        m_stagingTex = nullptr;
    }
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;

    // Release old reader
    if (m_reader) {
        m_reader->Release();
        m_reader = nullptr;
    }

    // Create new reader WITHOUT D3D manager — forces software decode
    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) return false;

    hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to set MF_LOW_LATENCY");
    }

    // Explicitly disable DXVA to force software decode
    hr = attrs->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to disable DXVA");
    }

    // Normalize path
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

    // Re-set output format
    if (!setOutputFormat()) {
        geode::log::warn("DecoderMF: fallback - failed to set output format");
        return false;
    }

    geode::log::info("DecoderMF: successfully switched to software decode");
    return true;
}

// ─────────────────────────────────────────────────────────────
// Seek
// ─────────────────────────────────────────────────────────────
void DecoderMF::seekTo(double seconds) {
    if (!m_reader) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();

    while (m_ring.nextRead()) m_ring.commitRead();

    PROPVARIANT var;
    var.vt = VT_I8;
    var.hVal.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);
    m_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    m_finished.store(false, std::memory_order_relaxed);
    if (wasDecoding) startDecoding();
}

// ─────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────
bool DecoderMF::consumeFrame(Frame& outFrame) {
    auto* slot = m_ring.nextRead();
    if (!slot) return false;

    // Copy Y plane — bulk when strides match
    if (outFrame.strideY == slot->strideY) {
        std::memcpy(outFrame.planeY, slot->planeY,
                    static_cast<size_t>(outFrame.strideY) * slot->height);
    } else {
        int copyBytes = std::min(outFrame.strideY, slot->strideY);
        for (int r = 0; r < slot->height; ++r)
            std::memcpy(outFrame.planeY + r * outFrame.strideY,
                        slot->planeY + r * slot->strideY, copyBytes);
    }

    // Copy Cb + Cr planes — bulk when strides match
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
int DecoderMF::getWidth()  const { return m_width; }
int DecoderMF::getHeight() const { return m_height; }
bool DecoderMF::isFinished() const {
    return m_finished.load(std::memory_order_acquire);
}

double DecoderMF::peekNextPTS() const {
    return m_ring.peekNextPTS();
}

// ─────────────────────────────────────────────────────────────
// Close
// ─────────────────────────────────────────────────────────────
void DecoderMF::closeInternal() {
    stopDecoding();

    // If the decode thread was detached (timedJoin timed out), wait for it to
    // actually finish before releasing COM/D3D resources it may still be using.
    if (m_threadRunning.load(std::memory_order_acquire)) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (m_threadRunning.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (m_threadRunning.load(std::memory_order_acquire)) {
            geode::log::warn("DecoderMF: thread still running during destruction after 500 ms wait");
        }
    }

    // Release D3D11 resources. Each decoder owns its own device, context and
    // DXGI manager. We hold the global mutex while releasing the device to
    // avoid concurrent device-destruction races in the GPU driver.
    {
        std::lock_guard lk(g_d3d11Mutex);
        if (m_dxgiMgr) {
            m_dxgiMgr->Release();
            m_dxgiMgr = nullptr;
        }
        if (m_d3dCtx) {
            m_d3dCtx->Release();
            m_d3dCtx = nullptr;
        }
        if (m_d3dDevice) {
            m_d3dDevice->Release();
            m_d3dDevice = nullptr;
        }
    }

    // During force close, MF DLLs (msmpeg2vdec.dll etc.) may already be unloaded
    // before the static singleton destructor runs. COM Release calls would crash
    // with ACCESS_VIOLATION because the vtable points into unloaded code.
    // Use SEH to handle this gracefully — just null out the pointers.
    __try {
        if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }
        if (m_reader) { m_reader->Release(); m_reader = nullptr; }
        // dxgiMgr / d3dCtx / d3dDevice already released above
        MFShutdown();
    } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
               ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        // MF DLLs already unloaded — just null out pointers, process is exiting
        m_stagingTex = nullptr;
        m_reader = nullptr;
    }
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;
    m_videoPath.clear();
}

} // namespace paimon

#endif // USE_MEDIA_FOUNDATION
