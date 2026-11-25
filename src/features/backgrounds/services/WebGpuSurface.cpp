#include "WebGpuSurface.hpp"
#include "WebGpuShared.hpp"

#ifdef _WIN32
#include <GL/gl.h>
#include <cstring>

namespace paimon::wallpaper {
namespace {

#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV 0x0000
#endif

using PFNOPENDEVICE = HANDLE(WINAPI*)(void* dxDevice);
using PFNCLOSEDEVICE = BOOL(WINAPI*)(HANDLE device);
using PFNREGISTEROBJECT = HANDLE(WINAPI*)(HANDLE device, void* dxObject, GLuint name, GLenum type, GLenum access);
using PFNUNREGISTEROBJECT = BOOL(WINAPI*)(HANDLE device, HANDLE object);
using PFNLOCKOBJECTS = BOOL(WINAPI*)(HANDLE device, GLint count, HANDLE* objects);
using PFNGETEXTENSIONSSTRING = char const*(WINAPI*)(HDC hdc);

struct InteropApi {
    PFNOPENDEVICE open = nullptr;
    PFNCLOSEDEVICE close = nullptr;
    PFNREGISTEROBJECT registerObject = nullptr;
    PFNUNREGISTEROBJECT unregisterObject = nullptr;
    PFNLOCKOBJECTS lock = nullptr;
    PFNLOCKOBJECTS unlock = nullptr;
    bool ready() const { return open && close && registerObject && unregisterObject && lock && unlock; }
};

template <typename T>
T loadProc(char const* name) {
    auto const address = reinterpret_cast<std::uintptr_t>(wglGetProcAddress(name));
    // WGL may answer 1, 2, 3 or -1 for missing entry points.
    if (address <= 3 || address == static_cast<std::uintptr_t>(-1)) return nullptr;
    return reinterpret_cast<T>(address);
}

// resolved per call: entry points belong to the current context.
InteropApi loadApi() {
    InteropApi api;
    if (!wglGetCurrentContext()) return api;
    auto const extensions = loadProc<PFNGETEXTENSIONSSTRING>("wglGetExtensionsStringARB");
    char const* list = extensions ? extensions(wglGetCurrentDC()) : nullptr;
    if (!list || !std::strstr(list, "WGL_NV_DX_interop2")) return api;
    api.open = loadProc<PFNOPENDEVICE>("wglDXOpenDeviceNV");
    api.close = loadProc<PFNCLOSEDEVICE>("wglDXCloseDeviceNV");
    api.registerObject = loadProc<PFNREGISTEROBJECT>("wglDXRegisterObjectNV");
    api.unregisterObject = loadProc<PFNUNREGISTEROBJECT>("wglDXUnregisterObjectNV");
    api.lock = loadProc<PFNLOCKOBJECTS>("wglDXLockObjectsNV");
    api.unlock = loadProc<PFNLOCKOBJECTS>("wglDXUnlockObjectsNV");
    return api;
}

} // namespace

struct WebGpuSurface::State {
    std::function<unsigned()> gen;
    std::function<void(unsigned)> del;
    InteropApi api;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HANDLE interopDevice = nullptr;
    HANDLE object = nullptr;
    GLuint name = 0;
    bool locked = false;
    bool hasContent = false;
    bool recreated = false;
    int width = 0;
    int height = 0;

    void dropTexture() {
        if (object) {
            if (locked) api.unlock(interopDevice, 1, &object);
            api.unregisterObject(interopDevice, object);
        }
        object = nullptr;
        locked = false;
        if (name) {
            if (del) del(name);
            else glDeleteTextures(1, &name);
        }
        name = 0;
        texture.Reset();
        width = height = 0;
        hasContent = false;
    }

    void dropDevice() {
        dropTexture();
        if (interopDevice) api.close(interopDevice);
        interopDevice = nullptr;
        device.Reset();
        context.Reset();
    }

    bool openDevice(WebGpuShared const& shared) {
        api = loadApi();
        if (!api.ready() || !shared.device || !shared.context) return false;
        interopDevice = api.open(shared.device.Get());
        if (!interopDevice) return false;
        device = shared.device;
        context = shared.context;
        return true;
    }

    bool ensureTexture(int w, int h) {
        if (texture && object && width == w && height == h) return true;
        dropTexture();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        // some drivers only map shareable resources.
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))) return false;
        if (gen) name = static_cast<GLuint>(gen());
        else glGenTextures(1, &name);
        if (!name) return false;
        object = api.registerObject(interopDevice, texture.Get(), name, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
        if (!object) return false;
        width = w;
        height = h;
        recreated = true;
        return true;
    }
};

WebGpuSurface::WebGpuSurface(std::function<unsigned()> genTexture, std::function<void(unsigned)> deleteTexture)
    : m_state(std::make_unique<State>()) {
    m_state->gen = std::move(genTexture);
    m_state->del = std::move(deleteTexture);
}

WebGpuSurface::~WebGpuSurface() {
    release();
}

bool WebGpuSurface::supported() {
    return loadApi().ready();
}

WebGpuSurface::Result WebGpuSurface::update(WebGpuShared const& shared, bool changed) {
    auto& s = *m_state;
    s.recreated = false;
    if (!shared.texture || shared.width <= 0 || shared.height <= 0) return Result::Unchanged;
    if (s.device.Get() != shared.device.Get()) {
        s.dropDevice();
        if (!s.openDevice(shared)) {
            s.dropDevice();
            return Result::Failed;
        }
    }
    bool const resized = s.width != shared.width || s.height != shared.height || !s.object;
    if (resized && !s.ensureTexture(shared.width, shared.height)) {
        s.dropDevice();
        return Result::Failed;
    }
    if (!changed && s.hasContent && !resized) return Result::Unchanged;

    // GL must not hold the resource while D3D writes it; locking again makes
    // the driver order the copy before the next GL sample.
    if (s.locked) {
        if (!s.api.unlock(s.interopDevice, 1, &s.object)) {
            s.dropDevice();
            return Result::Failed;
        }
        s.locked = false;
    }
    s.context->CopyResource(s.texture.Get(), shared.texture.Get());
    if (!s.api.lock(s.interopDevice, 1, &s.object)) {
        s.dropDevice();
        return Result::Failed;
    }
    s.locked = true;
    s.hasContent = true;
    return Result::Updated;
}

unsigned WebGpuSurface::texture() const { return m_state->locked ? m_state->name : 0; }
int WebGpuSurface::width() const { return m_state->width; }
int WebGpuSurface::height() const { return m_state->height; }
bool WebGpuSurface::textureRecreated() const { return m_state->recreated; }

void WebGpuSurface::release() {
    if (!m_state) return;
    if (m_state->interopDevice || m_state->name) m_state->dropDevice();
}

} // namespace paimon::wallpaper

#else

namespace paimon::wallpaper {
struct WebGpuSurface::State {};
WebGpuSurface::WebGpuSurface(std::function<unsigned()>, std::function<void(unsigned)>) : m_state(std::make_unique<State>()) {}
WebGpuSurface::~WebGpuSurface() = default;
bool WebGpuSurface::supported() { return false; }
WebGpuSurface::Result WebGpuSurface::update(WebGpuShared const&, bool) { return Result::Failed; }
unsigned WebGpuSurface::texture() const { return 0; }
int WebGpuSurface::width() const { return 0; }
int WebGpuSurface::height() const { return 0; }
bool WebGpuSurface::textureRecreated() const { return false; }
void WebGpuSurface::release() {}
} // namespace paimon::wallpaper

#endif
