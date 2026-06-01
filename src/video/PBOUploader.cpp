#include "PBOUploader.hpp"
#include "VideoDecoder.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/platform/CCEGLViewProtocol.h>
#include <cstring>
#include <thread>

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

// Function pointers for GLES3 PBO / fence sync (loaded at runtime).
// All of these are in GLES3 core; on a GLES2 context they return nullptr
// and the caller falls back to direct upload (see init()).
typedef void* (*PFN_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (*PFN_glUnmapBuffer)(GLenum);
typedef GLsync (*PFN_glFenceSync)(GLenum, GLbitfield);
typedef GLenum (*PFN_glClientWaitSync)(GLsync, GLbitfield, GLuint64);
typedef void (*PFN_glDeleteSync)(GLsync);

static PFN_glMapBufferRange pglMapBufferRange  = nullptr;
static PFN_glUnmapBuffer   pglUnmapBuffer      = nullptr;
static PFN_glFenceSync     pglFenceSync        = nullptr;
static PFN_glClientWaitSync pglClientWaitSync  = nullptr;
static PFN_glDeleteSync    pglDeleteSync       = nullptr;

static void loadGLSyncFunctions() {
    if (pglMapBufferRange) return;
    pglMapBufferRange  = (PFN_glMapBufferRange)eglGetProcAddress("glMapBufferRange");
    pglUnmapBuffer     = (PFN_glUnmapBuffer)eglGetProcAddress("glUnmapBuffer");
    // GLES3 fence sync
    pglFenceSync       = (PFN_glFenceSync)eglGetProcAddress("glFenceSync");
    pglClientWaitSync  = (PFN_glClientWaitSync)eglGetProcAddress("glClientWaitSync");
    pglDeleteSync      = (PFN_glDeleteSync)eglGetProcAddress("glDeleteSync");

    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::warn("PBOUploader: glMapBufferRange/glUnmapBuffer not available on this device");
        pglMapBufferRange = nullptr;
        pglUnmapBuffer    = nullptr;
    }
    if (!pglFenceSync || !pglClientWaitSync || !pglDeleteSync) {
        geode::log::warn("PBOUploader: GLES3 fence sync not available; PBO rotation will be unprotected");
        pglFenceSync      = nullptr;
        pglClientWaitSync = nullptr;
        pglDeleteSync     = nullptr;
    }
}

// Redirect glMapBufferRange / glUnmapBuffer to our pointers
#define glMapBufferRange pglMapBufferRange
#undef glUnmapBuffer
#define glUnmapBuffer    pglUnmapBuffer

// Redirect fence calls — if pointers are null they return stub values that
// make checkAndClearFence treat the slot as always ready (safe fallback).
#undef glFenceSync
#undef glClientWaitSync
#undef glDeleteSync
#define glFenceSync       pglFenceSync
#define glClientWaitSync  pglClientWaitSync
#define glDeleteSync      pglDeleteSync

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

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
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
    for (int i = 0; i < m_activeSlots; ++i) {
        if (m_slots[i].fence) {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
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

    m_ownerThread = std::this_thread::get_id();
    loadGLSyncFunctions();
    while (glGetError() != GL_NO_ERROR) {}

    // Pick a runtime slot count based on frame size to balance memory vs performance.
    // More slots = less chance of all PBOs being busy = fewer stalls.
    // 4K+: 3 slots (was 2) - ~36 MB total
    // 1080p-1440p: 5 slots (was 3) - ~25 MB total
    // 720p and below: 6 slots (was 3) - ~12 MB total
    int totalBytes = ySize + cbSize + crSize;
    if (totalBytes > 12 * 1024 * 1024) {
        m_activeSlots = 3;  // 4K+: increased from 2 to reduce stalls
    } else if (totalBytes > 4 * 1024 * 1024) {
        m_activeSlots = 5;  // 1080p-1440p: increased from 3
    } else {
        m_activeSlots = 6;  // 720p: increased from 3
    }

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
                glDeleteBuffers(i + 1, pbos);
                return false;
            }
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return true;
    };

    GLuint pboY[kPBOCount], pboCb[kPBOCount], pboCr[kPBOCount];
    if (!allocPBOs(pboY,  m_activeSlots, ySize))  { shutdown(); return false; }
    for (int i = 0; i < m_activeSlots; ++i) {
        m_slots[i].pboY    = pboY[i];
        m_slots[i].pboCb   = 0;
        m_slots[i].pboCr   = 0;
        m_slots[i].pboRGBA = 0;
        m_slots[i].fence   = nullptr;
    }
    m_initialized = true; // desde aquí shutdown() limpia lo que haya

    if (!allocPBOs(pboCb, m_activeSlots, cbSize)) { shutdown(); return false; }
    for (int i = 0; i < m_activeSlots; ++i) m_slots[i].pboCb = pboCb[i];
    if (!allocPBOs(pboCr, m_activeSlots, crSize)) { shutdown(); return false; }
    for (int i = 0; i < m_activeSlots; ++i) m_slots[i].pboCr = pboCr[i];

    m_uploadIdx = 0;

    geode::log::info("PBOUploader: initialized YUV mode (Y={} Cb={} Cr={} bytes, {} slots with fences)",
                     ySize, cbSize, crSize, m_activeSlots);
    return true;
}

bool PBOUploader::init(int rgbaSize) {
    if (m_initialized) shutdown();

    m_ownerThread = std::this_thread::get_id();
    loadGLSyncFunctions();
    while (glGetError() != GL_NO_ERROR) {}

    m_rgbaMode = true;
    m_rgbaSize = rgbaSize;

    // RGBA slot count: balance memory vs performance to reduce PBO busy stalls.
    // 4K+: 3 slots (was 2) - ~99 MB total
    // 1080p-1440p: 5 slots (was 3) - ~40 MB total
    // 720p: 6 slots (was 3) - ~11 MB total
    if (rgbaSize > 20 * 1024 * 1024) {
        m_activeSlots = 3;  // 4K+: increased from 2
    } else if (rgbaSize > 6 * 1024 * 1024) {
        m_activeSlots = 5;  // 1080p-1440p: increased from 3
    } else {
        m_activeSlots = 6;  // 720p: increased from 3
    }

#if defined(GEODE_IS_ANDROID)
    // Android: only use PBO if glMapBufferRange is available (GLES3 context).
    // GLES2 contexts can't map PBOs, so we fall back to direct upload.
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::info("PBOUploader: glMapBufferRange unavailable (GLES2 context) — "
                         "using direct texture upload");
        return false;
    }
#elif defined(GEODE_IS_MACOS)
    // macOS legacy GL 2.1 context may not expose glMapBufferRange.  If so,
    // fall back to direct upload rather than segfaulting on a null pointer.
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::info("PBOUploader: glMapBufferRange unavailable on this macOS GL context — "
                         "using direct texture upload");
        return false;
    }
#endif

    GLuint pboRGBA[kPBOCount];
    glGenBuffers(m_activeSlots, pboRGBA);
    for (int i = 0; i < m_activeSlots; ++i) {
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

    for (int i = 0; i < m_activeSlots; ++i) {
        m_slots[i].pboY    = 0;
        m_slots[i].pboCb   = 0;
        m_slots[i].pboCr   = 0;
        m_slots[i].pboRGBA = pboRGBA[i];
        m_slots[i].fence   = nullptr;
    }

    m_uploadIdx   = 0;
    m_initialized = true;

    geode::log::info("PBOUploader: initialized RGBA mode ({} bytes, {} slots with fences)",
                     rgbaSize, m_activeSlots);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Shutdown — delete all PBOs + fences (must be called from GL thread)
// ─────────────────────────────────────────────────────────────
void PBOUploader::shutdown() {
    if (!m_initialized) return;

    // ── GL/main-thread safety check ──
    // gl* calls are only valid on the main thread with a live GL context. If
    // either condition fails, "forget" the GL handles and let the OS reclaim
    // them when the process exits. Calling glDelete* off-thread is undefined
    // behavior on most drivers (crash on Windows, no-op on others).
    bool isMainThread = std::this_thread::get_id() == m_ownerThread;
    bool glContextAlive = cocos2d::CCDirector::get()
        && cocos2d::CCDirector::get()->getOpenGLView();
    if (!glContextAlive || !isMainThread) {
        for (int i = 0; i < kPBOCount; ++i) {
            m_slots[i] = {};
        }
        m_mappedSlotIdx = -1;
        m_activeSlots = kPBOCount;
        m_initialized = false;
        return;
    }

    // If a tryBegin was never finalised with endRGBAUpload, unmap now.
    if (m_mappedSlotIdx >= 0) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_slots[m_mappedSlotIdx].pboRGBA);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        m_mappedSlotIdx = -1;
    }

    deleteAllFences();

    if (m_rgbaMode) {
        GLuint pbos[kPBOCount];
        for (int i = 0; i < m_activeSlots; ++i) pbos[i] = m_slots[i].pboRGBA;
        glDeleteBuffers(m_activeSlots, pbos);
    } else {
        GLuint pY[kPBOCount], pCb[kPBOCount], pCr[kPBOCount];
        for (int i = 0; i < m_activeSlots; ++i) {
            pY[i]  = m_slots[i].pboY;
            pCb[i] = m_slots[i].pboCb;
            pCr[i] = m_slots[i].pboCr;
        }
        glDeleteBuffers(m_activeSlots, pY);
        glDeleteBuffers(m_activeSlots, pCb);
        glDeleteBuffers(m_activeSlots, pCr);
    }

    for (int i = 0; i < kPBOCount; ++i) {
        m_slots[i] = {};
    }

    m_activeSlots = kPBOCount;
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
    for (int attempt = 0; attempt < m_activeSlots; ++attempt) {
        int idx = (startIdx + attempt) % m_activeSlots;
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
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // Rotate to next slot for next frame
    m_uploadIdx = (m_uploadIdx + 1) % m_activeSlots;
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
    for (int attempt = 0; attempt < m_activeSlots; ++attempt) {
        int idx = (startIdx + attempt) % m_activeSlots;
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
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // Rotate to next slot for next frame
    m_uploadIdx = (m_uploadIdx + 1) % m_activeSlots;
    return true;
}

PBOUploader::~PBOUploader() {
    // Note: if GL context is still active, shutdown() cleans up.
    // If context is already destroyed, GL calls would fail —
    // caller should call shutdown() before context teardown.
    shutdown();
}

// ─────────────────────────────────────────────────────────────
// Zero-copy upload — map PBO, return writable pointer
// ─────────────────────────────────────────────────────────────
uint8_t* PBOUploader::tryBeginRGBAUpload(int width, int height) {
    if (!m_initialized || !m_rgbaMode) return nullptr;
    if (m_mappedSlotIdx >= 0) {
        // Begin called twice without matching end — refuse to re-map.
        geode::log::warn("PBOUploader: tryBeginRGBAUpload called while another upload in progress");
        return nullptr;
    }

    // Ensure the PBO is still big enough for the requested frame size.
    // Use 64-bit math so width*height*4 cannot overflow into a small (or
    // negative) positive value that would slip past the size check and let
    // a subsequent memcpy run past the end of the PBO.
    int64_t needed64 = static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4;
    if (needed64 <= 0 || needed64 > static_cast<int64_t>(m_rgbaSize)) return nullptr;
    int needed = static_cast<int>(needed64);
    (void)needed;

    // Find a slot whose fence is signalled.
    int startIdx = m_uploadIdx;
    int chosen = -1;
    for (int attempt = 0; attempt < m_activeSlots; ++attempt) {
        int idx = (startIdx + attempt) % m_activeSlots;
        if (checkAndClearFence(idx)) {
            chosen = idx;
            break;
        }
    }
    if (chosen < 0) return nullptr;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_slots[chosen].pboRGBA);
    void* mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, m_rgbaSize,
                                     GL_MAP_WRITE_BIT |
                                     GL_MAP_INVALIDATE_BUFFER_BIT |
                                     GL_MAP_UNSYNCHRONIZED_BIT);
    if (!mapped) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return nullptr;
    }

    m_mappedSlotIdx = chosen;
    return static_cast<uint8_t*>(mapped);
}

void PBOUploader::endRGBAUpload(GLuint texId, int width, int height) {
    if (m_mappedSlotIdx < 0) return;
    int idx = m_mappedSlotIdx;
    m_mappedSlotIdx = -1;

    // PBO is still bound from tryBegin.
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Fence AFTER upload — GPU signals when done reading the PBO.
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[idx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    m_uploadIdx = (idx + 1) % m_activeSlots;
}

} // namespace paimon::video
