#pragma once

#include <cstdint>
#include <thread>

#if defined(GEODE_IS_WINDOWS)
#include <gl/gl.h>
// msvc's gl header may omit glsync.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
// gles2 has no glsync type.
typedef struct __GLsync* GLsync;
#elif defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
// fall back if the apple extension header omits glsync.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
typedef struct __GLsync* GLsync;
#endif
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

namespace paimon::video {

// fenced rotating slots, gl thread only; busy slots defer the upload.

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

    // zero-copy: no calls between begin/end; nullptr means use uploadrgba.
    uint8_t* tryBeginRGBAUpload(int width, int height);
    void endRGBAUpload(GLuint texId, int width, int height);

    bool isInitialized() const { return m_initialized; }

    // clear pending fences, e.g. after a seek.
    void clearFences() { deleteAllFences(); }

private:
    int claimReadySlot();
    bool isSlotReady(int idx);
    bool checkAndClearFence(int idx);
    void deleteAllFences();

    static constexpr int kPBOCount = 6;

    PBOSlot m_slots[kPBOCount];
    int     m_activeSlots = kPBOCount;

    int m_ySize  = 0;
    int m_cbSize = 0;
    int m_crSize = 0;
    int m_rgbaSize = 0;

    bool m_rgbaMode = false;

    int m_uploadIdx = 0;
    int m_mappedSlotIdx = -1;
    bool m_initialized = false;

    // owner thread; shutdown refuses gl calls from another thread.
    std::thread::id m_ownerThread{};
};

}
