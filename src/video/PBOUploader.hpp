#pragma once

#include <cstdint>

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

// ─────────────────────────────────────────────────────────────
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
// ─────────────────────────────────────────────────────────────

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

    // Allocate PBOs for YUV plane sizes (9 PBOs total).
    // Call once after GL context is available.
    bool init(int ySize, int cbSize, int crSize);

    // Allocate PBOs for a single RGBA plane (3 PBOs).
    bool init(int rgbaSize);

    // Release all GL resources (PBOs + fences).  Must be called from the GL thread.
    void shutdown();

    // Upload a YUV frame into three separate textures (Y, Cb, Cr).
    // Returns true if uploaded, false if all PBO slots are still in use by GPU.
    bool upload(GLuint texY, GLuint texCb, GLuint texCr,
                const uint8_t* planeY,  int strideY,
                const uint8_t* planeCb, int strideCb,
                const uint8_t* planeCr, int strideCr,
                int width, int height);

    // Upload a single RGBA buffer into one texture via PBO.
    // Returns true if uploaded, false if all PBO slots are still in use by GPU.
    bool uploadRGBA(GLuint texId, const uint8_t* rgbaData, int width, int height);

    bool isInitialized() const { return m_initialized; }

    // Clear all pending fences — needed when restarting video after seek
    void clearFences() { deleteAllFences(); }

private:
    // Check if a slot's fence is signaled (non-blocking).  Returns true if safe to use.
    bool isSlotReady(int idx);

    // Wait for fence with timeout=0, delete if signaled.  Returns true if slot is ready.
    bool checkAndClearFence(int idx);

    // Delete all outstanding fences (for shutdown/seek/stop).
    void deleteAllFences();

    void uploadPlane(int slotIdx, GLuint texId, GLenum format,
                     const uint8_t* data, int stride, int width, int height);

    static constexpr int kPBOCount = 5;  // 5-slot rotation for maximum GPU throughput

    PBOSlot m_slots[kPBOCount];

    int m_ySize  = 0;
    int m_cbSize = 0;
    int m_crSize = 0;
    int m_rgbaSize = 0;

    bool m_rgbaMode = false;  // true = single RGBA plane, false = YUV 3-plane

    int m_uploadIdx = 0;  // rotates 0..2 each upload
    bool m_initialized = false;
};

} // namespace paimon::video
