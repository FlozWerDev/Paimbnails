#pragma once

#include <cstdint>
#include <thread>

#if defined(GEODE_IS_WINDOWS)
#include <gl/gl.h>
// MSVC's GL header may omit GLsync.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
// GLES2 has no GLsync type.
typedef struct __GLsync* GLsync;
#elif defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
// Fall back if the Apple extension header omits GLsync.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

namespace paimon::video {

// Async PBO uploads use fenced rotating slots. Busy slots are skipped and the
// upload is deferred; all methods run on the GL thread.

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

    // Zero-copy contract: tryBeginRGBAUpload → fill mapped RGBA bytes →
    // endRGBAUpload. Do not call other methods between them; nullptr means
    // use uploadRGBA. Mapping is unavailable on GLES2 and older macOS.
    uint8_t* tryBeginRGBAUpload(int width, int height);
    void endRGBAUpload(GLuint texId, int width, int height);

    bool isInitialized() const { return m_initialized; }

    // Clear pending fences, e.g. after a seek.
    void clearFences() { deleteAllFences(); }

private:
    bool isSlotReady(int idx);
    bool checkAndClearFence(int idx);
    void deleteAllFences();

    void uploadPlane(int slotIdx, GLuint texId, GLenum format,
                     const uint8_t* data, int stride, int width, int height);

    // Runtime slot count is capped by kPBOCount.
    static constexpr int kPBOCount = 6;

    PBOSlot m_slots[kPBOCount];
    int     m_activeSlots = kPBOCount;

    int m_ySize  = 0;
    int m_cbSize = 0;
    int m_crSize = 0;
    int m_rgbaSize = 0;

    bool m_rgbaMode = false;  // single RGBA vs. three-plane YUV

    int m_uploadIdx = 0;
    // Slot currently in a tryBegin→end sequence (-1 = none).
    int m_mappedSlotIdx = -1;
    bool m_initialized = false;

    // Owner thread; shutdown refuses GL calls from another thread.
    std::thread::id m_ownerThread{};
};

}
