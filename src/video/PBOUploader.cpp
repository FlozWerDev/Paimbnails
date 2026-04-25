#include "PBOUploader.hpp"
#include "VideoDecoder.hpp"
#include <Geode/loader/Log.hpp>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// GL sync function pointers — loaded dynamically on Windows
// because <gl/gl.h> only declares OpenGL 1.1.
// On macOS / iOS, <OpenGL/gl.h> provides them directly.
// On Android / GLES2, fences require GL_OES_EGL_sync extension.
// ─────────────────────────────────────────────────────────────
#if defined(GEODE_IS_WINDOWS)
#include <windows.h>

// Define GL sync types locally — <gl/gl.h> only provides GL 1.1,
// and <GL/glext.h> may conflict with Geode SDK's GL headers.
typedef GLsync  (GLAPIENTRY* PFN_FENCESYNC)(GLenum, GLbitfield);
typedef GLenum  (GLAPIENTRY* PFN_CLIENTWAITSYNC)(GLsync, GLbitfield, GLuint64);
typedef void    (GLAPIENTRY* PFN_DELETESYNC)(GLsync);

static PFN_FENCESYNC       pglFenceSync       = nullptr;
static PFN_CLIENTWAITSYNC  pglClientWaitSync  = nullptr;
static PFN_DELETESYNC      pglDeleteSync      = nullptr;

static void loadGLSyncFunctions() {
    if (pglFenceSync) return;  // already loaded
    auto* dll = GetModuleHandleA("opengl32.dll");
    if (!dll) dll = GetModuleHandleA("OPENGL32.dll");

    // wglGetProcAddress is the correct way to get GL extension function pointers
    pglFenceSync      = (PFN_FENCESYNC)wglGetProcAddress("glFenceSync");
    pglClientWaitSync = (PFN_CLIENTWAITSYNC)wglGetProcAddress("glClientWaitSync");
    pglDeleteSync     = (PFN_DELETESYNC)wglGetProcAddress("glDeleteSync");

    if (!pglFenceSync || !pglClientWaitSync || !pglDeleteSync) {
        geode::log::warn("PBOUploader: GL sync functions not available — fence sync disabled");
        pglFenceSync      = nullptr;
        pglClientWaitSync = nullptr;
        pglDeleteSync     = nullptr;
    }
}

// Define macros so the rest of the code uses the function pointers transparently
// Undefine GLEW macros first to avoid -Wmacro-redefined warnings
#undef glFenceSync
#undef glClientWaitSync
#undef glDeleteSync
#define glFenceSync       pglFenceSync
#define glClientWaitSync  pglClientWaitSync
#define glDeleteSync      pglDeleteSync
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_TIMEOUT_EXPIRED            0x911B
#define GL_ALREADY_SIGNALED           0x911A
#define GL_CONDITION_SATISFIED        0x911C

#elif defined(GEODE_IS_ANDROID)
// ── Android: GLES2 header + manually declared GLES3 PBO symbols ──
// We can't include <GLES3/gl3.h> because it conflicts with Geode's
// CCGL.h vertex-array macros.  Instead we declare only what we need.
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

// GL constants not in GLES2 headers
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif

// Function pointers for GLES3 PBO functions (loaded at runtime)
typedef void* (*PFN_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (*PFN_glUnmapBuffer)(GLenum);

static PFN_glMapBufferRange pglMapBufferRange = nullptr;
static PFN_glUnmapBuffer   pglUnmapBuffer    = nullptr;

static void loadGLSyncFunctions() {
    if (pglMapBufferRange) return;
    pglMapBufferRange = (PFN_glMapBufferRange)eglGetProcAddress("glMapBufferRange");
    pglUnmapBuffer    = (PFN_glUnmapBuffer)eglGetProcAddress("glUnmapBuffer");
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::warn("PBOUploader: glMapBufferRange/glUnmapBuffer not available on this device");
        pglMapBufferRange = nullptr;
        pglUnmapBuffer    = nullptr;
    }
}

// Redirect glMapBufferRange / glUnmapBuffer to our pointers
#define glMapBufferRange pglMapBufferRange
#undef glUnmapBuffer
#define glUnmapBuffer    pglUnmapBuffer

// Stub out fence calls on Android (PBO rotation still works without them)
#undef glFenceSync
#undef glClientWaitSync
#undef glDeleteSync
#define glFenceSync(...)       nullptr
#define glClientWaitSync(...)  GL_TIMEOUT_EXPIRED
#define glDeleteSync(...)

#elif defined(GEODE_IS_IOS)
// ── iOS: ES2 + APPLE extension variants for sync + PBO ──
// <OpenGLES/ES2/glext.h> provides the APPLE-suffixed sync functions.
// PBO constants and glMapBufferRange are in the EXT extension.

#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED GL_TIMEOUT_EXPIRED_APPLE
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED GL_ALREADY_SIGNALED_APPLE
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED GL_CONDITION_SATISFIED_APPLE
#endif

// Map standard GL sync calls to APPLE variants
#define glFenceSync(cond, flags)              glFenceSyncAPPLE(cond, flags)
#define glClientWaitSync(sync, flags, timeout) glClientWaitSyncAPPLE(sync, flags, timeout)
#define glDeleteSync(sync)                    glDeleteSyncAPPLE(sync)

// glMapBufferRange / glUnmapBuffer — available via EXT/OES on iOS
#ifndef glMapBufferRange
#define glMapBufferRange glMapBufferRangeEXT
#endif
#ifndef glUnmapBuffer
#define glUnmapBuffer glUnmapBufferOES
#endif

static void loadGLSyncFunctions() {}  // no-op — symbols available at link time

#elif defined(GEODE_IS_MACOS)
// macOS — <OpenGL/gl.h> provides sync natively but GL_MAP_* / glMapBufferRange
// are GL 3.0+ and not declared in the legacy GL 2.1 headers cocos2d uses.
// Load them dynamically via dlsym.
#include <dlfcn.h>
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif

typedef void* (*PFN_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (*PFN_glUnmapBuffer)(GLenum);

static PFN_glMapBufferRange pglMapBufferRange = nullptr;
static PFN_glUnmapBuffer   pglUnmapBuffer    = nullptr;

static void loadGLSyncFunctions() {
    if (pglMapBufferRange) return;
    pglMapBufferRange = (PFN_glMapBufferRange)dlsym(RTLD_DEFAULT, "glMapBufferRange");
    pglUnmapBuffer    = (PFN_glUnmapBuffer)dlsym(RTLD_DEFAULT, "glUnmapBuffer");
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::warn("PBOUploader: glMapBufferRange/glUnmapBuffer not available on this macOS GL context");
        pglMapBufferRange = nullptr;
        pglUnmapBuffer    = nullptr;
    }
}

#define glMapBufferRange pglMapBufferRange
#undef glUnmapBuffer
#define glUnmapBuffer    pglUnmapBuffer
#endif

namespace paimon::video {

// ─────────────────────────────────────────────────────────────
// Fence helpers
// ─────────────────────────────────────────────────────────────
bool PBOUploader::checkAndClearFence(int idx) {
    GLsync& fence = m_slots[idx].fence;
    if (!fence) return true;  // no fence → slot is ready

#if defined(GEODE_IS_WINDOWS)
    // If sync functions aren't available, always allow reuse
    if (!glFenceSync || !glClientWaitSync || !glDeleteSync) {
        fence = nullptr;
        return true;
    }
#endif

    // Non-blocking check: timeout=0 means "return immediately"
    GLenum result = glClientWaitSync(fence, 0, 0);
    if (result == GL_TIMEOUT_EXPIRED) {
        // GPU still reading this PBO — not safe to reuse
        return false;
    }

    // Either GL_ALREADY_SIGNALED or GL_CONDITION_SATISFIED → safe to reuse
    glDeleteSync(fence);
    fence = nullptr;
    return true;
}

bool PBOUploader::isSlotReady(int idx) {
    return checkAndClearFence(idx);
}

void PBOUploader::deleteAllFences() {
    for (int i = 0; i < kPBOCount; ++i) {
        if (m_slots[i].fence) {
#if defined(GEODE_IS_WINDOWS)
            if (glDeleteSync)
#endif
                glDeleteSync(m_slots[i].fence);
            m_slots[i].fence = nullptr;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Init — allocate 9 PBOs (3 per YUV plane) into PBOSlots
// ─────────────────────────────────────────────────────────────
bool PBOUploader::init(int ySize, int cbSize, int crSize) {
    if (m_initialized) shutdown();

    loadGLSyncFunctions();
    while (glGetError() != GL_NO_ERROR) {}

    m_rgbaMode = false;
    m_ySize  = ySize;
    m_cbSize = cbSize;
    m_crSize = crSize;

    auto allocPBOs = [](GLuint* pbos, int count, int size) -> bool {
        glGenBuffers(count, pbos);
        for (int i = 0; i < count; ++i) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
            glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);
            if (glGetError() != GL_NO_ERROR) {
                geode::log::warn("PBOUploader: glBufferData failed for PBO {}", i);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                return false;
            }
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return true;
    };

    GLuint pboY[kPBOCount], pboCb[kPBOCount], pboCr[kPBOCount];
    if (!allocPBOs(pboY,  kPBOCount, ySize))  { shutdown(); return false; }
    if (!allocPBOs(pboCb, kPBOCount, cbSize)) { shutdown(); return false; }
    if (!allocPBOs(pboCr, kPBOCount, crSize)) { shutdown(); return false; }

    for (int i = 0; i < kPBOCount; ++i) {
        m_slots[i].pboY    = pboY[i];
        m_slots[i].pboCb   = pboCb[i];
        m_slots[i].pboCr   = pboCr[i];
        m_slots[i].pboRGBA = 0;
        m_slots[i].fence   = nullptr;
    }

    m_uploadIdx   = 0;
    m_initialized = true;

    geode::log::info("PBOUploader: initialized YUV mode (Y={} Cb={} Cr={} bytes, {} slots with fences)",
                     ySize, cbSize, crSize, kPBOCount);
    return true;
}

bool PBOUploader::init(int rgbaSize) {
    if (m_initialized) shutdown();

    loadGLSyncFunctions();
    while (glGetError() != GL_NO_ERROR) {}

    m_rgbaMode = true;
    m_rgbaSize = rgbaSize;

#if defined(GEODE_IS_ANDROID)
    geode::log::warn("PBOUploader: disabled on Android; using direct texture upload");
    return false;
#endif

    GLuint pboRGBA[kPBOCount];
    glGenBuffers(kPBOCount, pboRGBA);
    for (int i = 0; i < kPBOCount; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboRGBA[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, rgbaSize, nullptr, GL_STREAM_DRAW);
        if (glGetError() != GL_NO_ERROR) {
            geode::log::warn("PBOUploader: glBufferData failed for RGBA PBO {}", i);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            shutdown();
            return false;
        }
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    for (int i = 0; i < kPBOCount; ++i) {
        m_slots[i].pboY    = 0;
        m_slots[i].pboCb   = 0;
        m_slots[i].pboCr   = 0;
        m_slots[i].pboRGBA = pboRGBA[i];
        m_slots[i].fence   = nullptr;
    }

    m_uploadIdx   = 0;
    m_initialized = true;

    geode::log::info("PBOUploader: initialized RGBA mode ({} bytes, {} slots with fences)",
                     rgbaSize, kPBOCount);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Shutdown — delete all PBOs + fences (must be called from GL thread)
// ─────────────────────────────────────────────────────────────
void PBOUploader::shutdown() {
    if (!m_initialized) return;

    deleteAllFences();

    if (m_rgbaMode) {
        GLuint pbos[kPBOCount];
        for (int i = 0; i < kPBOCount; ++i) pbos[i] = m_slots[i].pboRGBA;
        glDeleteBuffers(kPBOCount, pbos);
    } else {
        GLuint pY[kPBOCount], pCb[kPBOCount], pCr[kPBOCount];
        for (int i = 0; i < kPBOCount; ++i) {
            pY[i]  = m_slots[i].pboY;
            pCb[i] = m_slots[i].pboCb;
            pCr[i] = m_slots[i].pboCr;
        }
        glDeleteBuffers(kPBOCount, pY);
        glDeleteBuffers(kPBOCount, pCb);
        glDeleteBuffers(kPBOCount, pCr);
    }

    for (int i = 0; i < kPBOCount; ++i) {
        m_slots[i] = {};
    }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────
// Upload a single YUV plane via PBO (inline helper)
// ─────────────────────────────────────────────────────────────
void PBOUploader::uploadPlane(int slotIdx, GLuint texId, GLenum format,
                               const uint8_t* data, int stride,
                               int width, int height) {
    // Select the PBO for this plane within the slot.
    // We match by texture ID to determine which plane PBO to use.
    // The caller (upload) passes texY/texCb/texCr — we store the
    // texture IDs from the first upload call to disambiguate.
    // Simpler approach: just use the slot's PBOs in sequence.
    // Since upload() calls us 3 times (Y, Cb, Cr), we use a
    // per-slot plane counter that resets each upload() call.

    // NOTE: This is kept for API compatibility but the YUV upload()
    // now inlines the per-plane logic directly. This method is unused
    // in the current flow.
    (void)slotIdx; (void)texId; (void)format;
    (void)data; (void)stride; (void)width; (void)height;
}

// Helper: write data into a single PBO and upload to texture
static void uploadSinglePBO(GLuint pbo, int pboSize, GLuint texId,
                             GLenum format, const uint8_t* data,
                             int stride, int width, int height) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    void* mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, pboSize,
                                     GL_MAP_WRITE_BIT |
                                     GL_MAP_INVALIDATE_BUFFER_BIT |
                                     GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped && data) {
        int rowBytes = (format == GL_RGBA) ? width * 4 : width;
        if (stride == rowBytes) {
            std::memcpy(mapped, data, static_cast<size_t>(rowBytes) * height);
        } else {
            auto* dst = static_cast<uint8_t*>(mapped);
            for (int r = 0; r < height; ++r) {
                std::memcpy(dst + r * rowBytes, data + r * stride, rowBytes);
            }
        }
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    } else if (data) {
        int rowBytes = (format == GL_RGBA) ? width * 4 : width;
        if (stride == rowBytes) {
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0,
                            static_cast<GLsizeiptr>(rowBytes) * height, data);
        }
        // stride mismatch + map failure: extremely rare, skip
    }

    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    format, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

// ─────────────────────────────────────────────────────────────
// Upload full YUV frame — with fence synchronization
// ─────────────────────────────────────────────────────────────
bool PBOUploader::upload(GLuint texY, GLuint texCb, GLuint texCr,
                          const uint8_t* planeY,  int strideY,
                          const uint8_t* planeCb, int strideCb,
                          const uint8_t* planeCr, int strideCr,
                          int width, int height) {
    if (!m_initialized) return false;

    // Find a slot whose fence is signaled (non-blocking)
    int startIdx = m_uploadIdx;
    bool found = false;
    for (int attempt = 0; attempt < kPBOCount; ++attempt) {
        int idx = (startIdx + attempt) % kPBOCount;
        if (checkAndClearFence(idx)) {
            m_uploadIdx = idx;
            found = true;
            break;
        }
    }
    if (!found) {
        // All slots still in use by GPU — defer to next update()
        return false;
    }

    int uvH = (height + 1) / 2;
    int uvW = (width + 1) / 2;

    // Y plane
    uploadSinglePBO(m_slots[m_uploadIdx].pboY, m_ySize, texY,
                    GL_LUMINANCE, planeY, strideY, width, height);
    // Cb plane
    uploadSinglePBO(m_slots[m_uploadIdx].pboCb, m_cbSize, texCb,
                    GL_LUMINANCE, planeCb, strideCb, uvW, uvH);
    // Cr plane
    uploadSinglePBO(m_slots[m_uploadIdx].pboCr, m_crSize, texCr,
                    GL_LUMINANCE, planeCr, strideCr, uvW, uvH);

    // Insert fence AFTER all glTexSubImage2D — GPU signals when done reading
#if defined(GEODE_IS_WINDOWS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // Rotate to next slot for next frame
    m_uploadIdx = (m_uploadIdx + 1) % kPBOCount;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Upload RGBA frame — with fence synchronization
// ─────────────────────────────────────────────────────────────
bool PBOUploader::uploadRGBA(GLuint texId, const uint8_t* rgbaData, int width, int height) {
    if (!m_initialized || !m_rgbaMode) return false;

    // Find a slot whose fence is signaled (non-blocking)
    int startIdx = m_uploadIdx;
    bool found = false;
    for (int attempt = 0; attempt < kPBOCount; ++attempt) {
        int idx = (startIdx + attempt) % kPBOCount;
        if (checkAndClearFence(idx)) {
            m_uploadIdx = idx;
            found = true;
            break;
        }
    }
    if (!found) {
        // All slots still in use by GPU — defer to next update()
        return false;
    }

    uploadSinglePBO(m_slots[m_uploadIdx].pboRGBA, m_rgbaSize, texId,
                    GL_RGBA, rgbaData, width * 4, width, height);

    // Insert fence AFTER glTexSubImage2D — GPU signals when done reading
#if defined(GEODE_IS_WINDOWS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // Rotate to next slot for next frame
    m_uploadIdx = (m_uploadIdx + 1) % kPBOCount;
    return true;
}

PBOUploader::~PBOUploader() {
    // Note: if GL context is still active, shutdown() cleans up.
    // If context is already destroyed, GL calls would fail —
    // caller should call shutdown() before context teardown.
    shutdown();
}

} // namespace paimon::video
