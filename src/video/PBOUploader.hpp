#pragma once

#include <cstdint>
#include <thread>

#if defined(GEODE_IS_WINDOWS)
#include <gl/gl.h>
// GLsync is GL 3.2+ — may not be in <gl/gl.h> (GL 1.1 only on MSVC)
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
// GLES2 doesn't have GLsync — define as opaque pointer
typedef struct __GLsync* GLsync;
#elif defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
// GLsync is provided by the APPLE extension in glext.h
// but define fallback just in case
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

namespace paimon::video {

// PBOUploader — async GPU upload via Pixel Buffer Objects
//
// Uses 3 PBOSlots in rotation so that glTexSubImage2D never
// stalls the pipeline.  Each slot carries a GLsync fence that
// prevents re-use until the GPU has finished reading the PBO.
//
// Fence policy: glClientWaitSync with timeout=0 (non-blocking).
// If the fence is not signaled, the slot is skipped and the
// upload is deferred to the next update() call.
//
// MUST be used exclusively from the GL/main thread.

struct PBOSlot {
    GLuint pboY    = 0;
    GLuint pboCb   = 0;
    GLuint pboCr   = 0;
    GLuint pboRGBA = 0;
    GLsync fence   = nullptr;
};

class PBOUploader {
public:
    PBOUploader() = default;
    ~PBOUploader();

    PBOUploader(const PBOUploader&) = delete;
    PBOUploader& operator=(const PBOUploader&) = delete;

    bool init(int ySize, int cbSize, int crSize);

    bool init(int rgbaSize);

    void shutdown();

    bool upload(GLuint texY, GLuint texCb, GLuint texCr,
                const uint8_t* planeY,  int strideY,
                const uint8_t* planeCb, int strideCb,
                const uint8_t* planeCr, int strideCr,
                int width, int height);

    bool uploadRGBA(GLuint texId, const uint8_t* rgbaData, int width, int height);

    // Zero-copy upload pattern — see implementation for contract details
    // 1) Call tryBeginRGBAUpload(w, h): returns a writable mapped pointer of
    //    w*h*4 bytes, or nullptr if all PBO slots are busy (caller should
    //    fall back to uploadRGBA with a staging buffer on next frame).
    // 2) Fill the returned pointer with RGBA bytes (e.g. via yuvToRgba).
    // 3) Call endRGBAUpload(texId, w, h) to unmap and upload to the texture.
    //
    // Between tryBeginRGBAUpload and endRGBAUpload no other PBOUploader
    // method may be called.  If tryBeginRGBAUpload returns nullptr, do NOT
    // call endRGBAUpload.
    //
    // This is not available when PBO mapping is unsupported (GLES2, older
    // macOS).  In that case tryBeginRGBAUpload returns nullptr and the
    // caller must use uploadRGBA instead.
    uint8_t* tryBeginRGBAUpload(int width, int height);
    void endRGBAUpload(GLuint texId, int width, int height);

    bool isInitialized() const { return m_initialized; }

    // Clear all pending fences — needed when restarting video after seek
    void clearFences() { deleteAllFences(); }

private:
    bool isSlotReady(int idx);
    bool checkAndClearFence(int idx);
    void deleteAllFences();

    void uploadPlane(int slotIdx, GLuint texId, GLenum format,
                     const uint8_t* data, int stride, int width, int height);

    // Max PBO slots used at runtime (720p uses 6, 1080p uses 5, 4K+ uses 3).
    // m_activeSlots is the live count (<= kPBOCount).
    static constexpr int kPBOCount = 6;

    PBOSlot m_slots[kPBOCount];
    int     m_activeSlots = kPBOCount;

    int m_ySize  = 0;
    int m_cbSize = 0;
    int m_crSize = 0;
    int m_rgbaSize = 0;

    bool m_rgbaMode = false;  // true = single RGBA plane, false = YUV 3-plane

    int m_uploadIdx = 0;  // rotates 0..(m_activeSlots-1) each upload
    // Index of the slot currently in a tryBegin→end sequence (-1 = none).
    int m_mappedSlotIdx = -1;
    bool m_initialized = false;

    // Thread that called init() — captured so shutdown() can refuse to call
    // gl* from a different thread (UB on most drivers).
    std::thread::id m_ownerThread{};
};

} // namespace paimon::video
