#include "PBOUploader.hpp"
#include "VideoDecoder.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/platform/CCEGLViewProtocol.h>
#include <cstring>
#include <thread>

// Load GL sync functions dynamically on Windows.
#if defined(GEODE_IS_WINDOWS)
#include <windows.h>

// MSVC's GL header exposes only GL 1.1 types;
typedef GLsync  (GLAPIENTRY* PFN_FENCESYNC)(GLenum, GLbitfield);
typedef GLenum  (GLAPIENTRY* PFN_CLIENTWAITSYNC)(GLsync, GLbitfield, GLuint64);
typedef void    (GLAPIENTRY* PFN_DELETESYNC)(GLsync);

static PFN_FENCESYNC       pglFenceSync       = nullptr;
static PFN_CLIENTWAITSYNC  pglClientWaitSync  = nullptr;
static PFN_DELETESYNC      pglDeleteSync      = nullptr;

static void loadGLSyncFunctions() {
    if (pglFenceSync) return;
    auto* dll = GetModuleHandleA("opengl32.dll");
    if (!dll) dll = GetModuleHandleA("OPENGL32.dll");

    pglFenceSync      = (PFN_FENCESYNC)wglGetProcAddress("glFenceSync");
    pglClientWaitSync = (PFN_CLIENTWAITSYNC)wglGetProcAddress("glClientWaitSync");
    pglDeleteSync     = (PFN_DELETESYNC)wglGetProcAddress("glDeleteSync");

    if (!pglFenceSync || !pglClientWaitSync || !pglDeleteSync) {
        geode::log::warn("PBOUploader: GL sync functions not available - fence sync disabled");
        pglFenceSync      = nullptr;
        pglClientWaitSync = nullptr;
        pglDeleteSync     = nullptr;
    }
}

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
// Android loads GLES3 PBO symbols at runtime.
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

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

// GLES3 PBO/fence functions loaded at runtime.
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

// PBO uploads are GLES3-only; function pointers alone are not a reliable gate,
// so check the actual context version.
static bool isGLES3Context() {
    static int cached = -1;
    if (cached < 0) {
        const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        cached = (ver && std::strstr(ver, "OpenGL ES 3")) ? 1 : 0;
        if (cached == 0) {
            geode::log::info("PBOUploader: GL context is not GLES3 ('{}') - "
                             "PBO uploads disabled, using direct texture upload",
                             ver ? ver : "null");
        }
    }
    return cached == 1;
}

static void loadGLSyncFunctions() {
    if (pglMapBufferRange) return;
    pglMapBufferRange  = (PFN_glMapBufferRange)eglGetProcAddress("glMapBufferRange");
    pglUnmapBuffer     = (PFN_glUnmapBuffer)eglGetProcAddress("glUnmapBuffer");
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

#define glMapBufferRange pglMapBufferRange
#undef glUnmapBuffer
#define glUnmapBuffer    pglUnmapBuffer

#undef glFenceSync
#undef glClientWaitSync
#undef glDeleteSync
#define glFenceSync       pglFenceSync
#define glClientWaitSync  pglClientWaitSync
#define glDeleteSync      pglDeleteSync

#elif defined(GEODE_IS_IOS)
// iOS uses ES2 plus APPLE sync/PBO extensions.

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

#define glFenceSync(cond, flags)              glFenceSyncAPPLE(cond, flags)
#define glClientWaitSync(sync, flags, timeout) glClientWaitSyncAPPLE(sync, flags, timeout)
#define glDeleteSync(sync)                    glDeleteSyncAPPLE(sync)

// iOS exposes map/unmap through EXT/OES.
#ifndef glMapBufferRange
#define glMapBufferRange glMapBufferRangeEXT
#endif
#ifndef glUnmapBuffer
#define glUnmapBuffer glUnmapBufferOES
#endif

static void loadGLSyncFunctions() {}

#elif defined(GEODE_IS_MACOS)
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

bool PBOUploader::checkAndClearFence(int idx) {
    GLsync& fence = m_slots[idx].fence;
    if (!fence) return true;

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (!glFenceSync || !glClientWaitSync || !glDeleteSync) {
        fence = nullptr;
        return true;
    }
#endif

    GLenum result = glClientWaitSync(fence, 0, 0);
    if (result == GL_TIMEOUT_EXPIRED) {
        return false;
    }

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

bool PBOUploader::init(int ySize, int cbSize, int crSize) {
    if (m_initialized) shutdown();

    m_ownerThread = std::this_thread::get_id();
    loadGLSyncFunctions();
    while (glGetError() != GL_NO_ERROR) {}

#if defined(GEODE_IS_ANDROID)
// GLES2 PBO uploads can silently no-op and produce black textures; use direct upload.
    if (!isGLES3Context() || !pglMapBufferRange || !pglUnmapBuffer) {
        return false;
    }
#elif defined(GEODE_IS_MACOS)
// uploadSinglePBO maps unconditionally, so reject legacy contexts here.
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::info("PBOUploader: glMapBufferRange unavailable on this macOS GL context - "
                         "using direct texture upload");
        return false;
    }
#endif

    int totalBytes = ySize + cbSize + crSize;
    if (totalBytes > 12 * 1024 * 1024) {
        m_activeSlots = 3;
    } else if (totalBytes > 4 * 1024 * 1024) {
        m_activeSlots = 5;
    } else {
        m_activeSlots = 6;
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
    m_initialized = true;

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

    if (rgbaSize > 20 * 1024 * 1024) {
        m_activeSlots = 3;
    } else if (rgbaSize > 6 * 1024 * 1024) {
        m_activeSlots = 5;
    } else {
        m_activeSlots = 6;
    }

#if defined(GEODE_IS_ANDROID)
// Android requires a real GLES3 context; resolved function pointers alone are insufficient.
    if (!isGLES3Context() || !pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::info("PBOUploader: PBO unavailable (GLES2 context) - "
                         "using direct texture upload");
        return false;
    }
#elif defined(GEODE_IS_MACOS)
    if (!pglMapBufferRange || !pglUnmapBuffer) {
        geode::log::info("PBOUploader: glMapBufferRange unavailable on this macOS GL context - "
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
// Free names created before m_initialized was set.
            glDeleteBuffers(m_activeSlots, pboRGBA);
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

void PBOUploader::shutdown() {
    if (!m_initialized) return;

// GL deletion requires the owner thread and a live context.
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

void PBOUploader::uploadPlane(int slotIdx, GLuint texId, GLenum format,
                               const uint8_t* data, int stride,
                               int width, int height) {
    (void)slotIdx; (void)texId; (void)format;
    (void)data; (void)stride; (void)width; (void)height;
}

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
    }

    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    format, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

bool PBOUploader::upload(GLuint texY, GLuint texCb, GLuint texCr,
                          const uint8_t* planeY,  int strideY,
                          const uint8_t* planeCb, int strideCb,
                          const uint8_t* planeCr, int strideCr,
                          int width, int height) {
    if (!m_initialized) return false;

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
        return false;
    }

    int uvH = (height + 1) / 2;
    int uvW = (width + 1) / 2;

    uploadSinglePBO(m_slots[m_uploadIdx].pboY, m_ySize, texY,
                    GL_LUMINANCE, planeY, strideY, width, height);
    uploadSinglePBO(m_slots[m_uploadIdx].pboCb, m_cbSize, texCb,
                    GL_LUMINANCE, planeCb, strideCb, uvW, uvH);
    uploadSinglePBO(m_slots[m_uploadIdx].pboCr, m_crSize, texCr,
                    GL_LUMINANCE, planeCr, strideCr, uvW, uvH);

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    m_uploadIdx = (m_uploadIdx + 1) % m_activeSlots;
    return true;
}

bool PBOUploader::uploadRGBA(GLuint texId, const uint8_t* rgbaData, int width, int height) {
    if (!m_initialized || !m_rgbaMode) return false;

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
        return false;
    }

    uploadSinglePBO(m_slots[m_uploadIdx].pboRGBA, m_rgbaSize, texId,
                    GL_RGBA, rgbaData, width * 4, width, height);

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[m_uploadIdx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    m_uploadIdx = (m_uploadIdx + 1) % m_activeSlots;
    return true;
}

PBOUploader::~PBOUploader() {
// Call shutdown() before context teardown.
    shutdown();
}

uint8_t* PBOUploader::tryBeginRGBAUpload(int width, int height) {
    if (!m_initialized || !m_rgbaMode) return nullptr;
    if (m_mappedSlotIdx >= 0) {
// Refuse nested zero-copy uploads.
        geode::log::warn("PBOUploader: tryBeginRGBAUpload called while another upload in progress");
        return nullptr;
    }

// Keep size math 64-bit to prevent allocation overflow.
    int64_t needed64 = static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4;
    if (needed64 <= 0 || needed64 > static_cast<int64_t>(m_rgbaSize)) return nullptr;
    int needed = static_cast<int>(needed64);
    (void)needed;

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

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_MACOS)
    if (glFenceSync)
#endif
        m_slots[idx].fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    m_uploadIdx = (idx + 1) % m_activeSlots;
}

}
